"""Train-only scaling for 24-hour histories and four hourly PM2.5 targets."""

from dataclasses import dataclass
from pathlib import Path
import numpy as np
import pandas as pd
from sklearn.preprocessing import StandardScaler
from .utils import FEATURES, HORIZON_HOURS, LOOKBACK_HOURS, read_json, write_json


@dataclass
class ScalerBundle:
    """Separate input-column statistics and a separate PM2.5 label scaler."""

    x: StandardScaler
    y: StandardScaler
    features: tuple = FEATURES

    @classmethod
    def fit(cls, values: pd.DataFrame, train_bounds):
        """Use each original train hour once, never overlapping windows."""
        train = _select_train_values(values, train_bounds)
        inputs = _fit_scaler(train.to_numpy(dtype=float), "features")
        targets = _fit_scaler(train[["pm25"]].to_numpy(dtype=float), "PM2.5")
        return cls(inputs, targets)

    def transform(self, X_raw, y_raw):
        _validate_windows(X_raw, y_raw)
        X = _transform_array(X_raw, self.x, len(FEATURES))
        y = _transform_array(y_raw, self.y, 1)
        return X, y

    def inverse_targets(self, y_scaled):
        """Preserve vector/matrix shape and return finite PM2.5 in ug/m3."""
        values = np.asarray(y_scaled, dtype=np.float64)
        if values.size == 0 or not np.isfinite(values).all():
            raise ValueError(
                "Predictions must be nonempty and finite before inverse scaling"
            )
        with np.errstate(over="ignore", invalid="ignore"):
            physical = self.y.inverse_transform(values.reshape(-1, 1)).reshape(
                values.shape
            )
        if not np.isfinite(physical).all():
            raise ValueError("Inverse scaling overflow, physical PM2.5 must be finite")
        return physical

    def save(self, path: Path):
        write_json(
            path,
            {
                "feature_order": list(self.features),
                "target": "pm25",
                "units": "ug/m3",
                "x": _scaler_parameters(self.x),
                "y": _scaler_parameters(self.y),
            },
        )

    @classmethod
    def load(cls, path: Path):
        raw = read_json(path)
        _validate_metadata(raw)
        inputs = _read_scaler(raw, "x", len(FEATURES))
        targets = _read_scaler(raw, "y", 1)
        return cls(inputs, targets)


def _select_train_values(values, bounds):
    selected = (values.index >= bounds.start) & (values.index < bounds.end_exclusive)
    return values.loc[selected, list(FEATURES)]


def _fit_scaler(values, name):
    # StandardScaler ignores NaN per column, infinity is not a missing observation.
    if np.isinf(values).any() or not np.isfinite(values).any(axis=0).all():
        raise ValueError(f"train: {name} need valid finite observations to fit scalers")
    return StandardScaler().fit(values)


def _validate_windows(X, y):
    if X.shape != (len(X), LOOKBACK_HOURS, len(FEATURES)):
        raise ValueError("Expected X shape (N,24,3)")
    if y.shape != (len(X), HORIZON_HOURS):
        raise ValueError("Expected y shape (N,4)")
    if not np.isfinite(X).all() or not np.isfinite(y).all():
        raise ValueError("Cannot scale missing or invalid windows")


def _transform_array(values, scaler, columns):
    with np.errstate(over="ignore", invalid="ignore"):
        scaled = scaler.transform(values.reshape(-1, columns))
        scaled = scaled.reshape(values.shape).astype(np.float32)
    if not np.isfinite(scaled).all():
        raise ValueError("Scaled windows exceed finite float32 range")
    return scaled


def _scaler_parameters(scaler):
    return {"mean": scaler.mean_.tolist(), "scale": scaler.scale_.tolist()}


def _validate_metadata(raw):
    if (
        raw.get("feature_order") != list(FEATURES)
        or raw.get("target") != "pm25"
        or raw.get("units") != "ug/m3"
    ):
        raise ValueError("Invalid scaler feature order or target")


def _read_scaler(raw, name, columns):
    section = raw.get(name, {})
    if not isinstance(section, dict):
        raise ValueError(f"Invalid {name} scaler")
    mean = _read_statistics(section, "mean", columns, name)
    scale = _read_statistics(section, "scale", columns, name)
    if np.any(scale <= 0):
        raise ValueError(f"Invalid {name} scaler scale, expected positive values")
    scaler = StandardScaler()
    scaler.mean_, scaler.scale_ = mean, scale
    scaler.n_features_in_ = columns
    return scaler


def _read_statistics(section, field, columns, name):
    numbers = section.get(field)
    if (
        not isinstance(numbers, list)
        or len(numbers) != columns
        or any(type(number) not in (int, float) for number in numbers)
    ):
        raise ValueError(
            f"Invalid {name} scaler {field}, expected {columns} JSON numbers"
        )
    values = np.asarray(numbers, dtype=float)
    if not np.isfinite(values).all():
        raise ValueError(f"Invalid {name} scaler {field}, expected finite values")
    return values


# Existing scripts use these entry points, the class owns the implementation.
def transform_windows(X_raw, y_raw, scalers):
    return scalers.transform(X_raw, y_raw)


def inverse_targets(y_scaled, scalers):
    return scalers.inverse_targets(y_scaled)


def save_scalers(scalers, path):
    scalers.save(path)


def load_scalers(path):
    return ScalerBundle.load(path)
