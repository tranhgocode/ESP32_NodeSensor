"""Physical-unit baselines, using exactly the same origins as neural models."""

import numpy as np
from .utils import FEATURES, HORIZON_HOURS, LOOKBACK_HOURS


def _history(X_raw):
    values = np.asarray(X_raw, dtype=np.float64)
    if (
        values.ndim != 3
        or values.shape[1:] != (LOOKBACK_HOURS, len(FEATURES))
        or not len(values)
        or not np.isfinite(values).all()
    ):
        raise ValueError("Baseline requires finite nonempty (N,24,3) raw history")
    return values


def persistence_forecast(X_raw: np.ndarray) -> np.ndarray:
    x = _history(X_raw)
    return np.repeat(x[:, -1, 0, None], HORIZON_HOURS, axis=1)


def seasonal_naive_forecast(X_raw: np.ndarray) -> np.ndarray:
    # t+1..t+4 use the same hours one day earlier: t-23..t-20.
    return _history(X_raw)[:, :HORIZON_HOURS, 0].copy()
