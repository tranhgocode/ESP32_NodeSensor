"""Nội suy trong lịch sử và chuẩn hóa chỉ dựa trên tập train cho các cửa sổ dự báo."""

from dataclasses import dataclass
import json
from pathlib import Path

import numpy as np
import pandas as pd
import sklearn
from sklearn.preprocessing import StandardScaler

from .utils import FEATURES, HORIZON_HOURS, LOOKBACK_HOURS, MAX_GAP_HOURS

NO_DONOR = -1
FEATURE_COUNT = len(FEATURES)


@dataclass
class PreparedHistory:
    """Các giá trị đã bù cùng metadata riêng về ô được bù và donor ban đầu."""

    values: pd.DataFrame
    imputed_mask: pd.DataFrame
    left_donor_times: pd.DataFrame
    right_donor_times: pd.DataFrame
    valid: bool


def interpolate_history_arrays(values, trusted_mask, epoch_ns, max_gap_hours):
    """Nội suy theo thời gian giữa hai quan sát gốc trong cùng cửa sổ.

    Neo phải có thể sau ô thiếu nhưng không muộn hơn origin dự báo.
    """
    if type(max_gap_hours) is not int or not 0 <= max_gap_hours <= MAX_GAP_HOURS:
        raise ValueError("Invalid gap limit")
    filled = np.where(trusted_mask, values, np.nan).astype(float, copy=True)
    filled[~np.isfinite(filled)] = np.nan
    imputed = np.zeros(filled.shape, dtype=bool)
    left_donors = np.full(filled.shape, NO_DONOR, dtype=np.int64)
    right_donors = np.full(filled.shape, NO_DONOR, dtype=np.int64)
    for column in range(filled.shape[1]):
        observed = np.flatnonzero(np.isfinite(filled[:, column]))
        for left, right in zip(observed[:-1], observed[1:]):
            gap = right - left - 1
            if not 0 < gap <= max_gap_hours:
                continue
            # Hai mốc neo đều là quan sát gốc có sẵn không muộn hơn origin.
            for row in range(left + 1, right):
                elapsed = int(epoch_ns[row]) - int(epoch_ns[left])
                span = int(epoch_ns[right]) - int(epoch_ns[left])
                fraction = elapsed / span
                filled[row, column] = filled[left, column] + fraction * (
                    filled[right, column] - filled[left, column]
                )
                imputed[row, column] = True
                left_donors[row, column] = epoch_ns[left]
                right_donors[row, column] = epoch_ns[right]
    return filled, imputed, left_donors, right_donors


def prepare_history(
    values: pd.DataFrame, trusted_mask: pd.DataFrame, max_gap_hours: int
) -> PreparedHistory:
    """Nội suy lịch sử 24 giờ và trả về mask cùng thời điểm hai đầu neo."""
    if (
        len(values) != LOOKBACK_HOURS
        or not values.index.equals(trusted_mask.index)
        or list(values.columns) != list(trusted_mask.columns)
    ):
        raise ValueError("History requires 24 aligned rows")
    filled, imputed, left_donors, right_donors = interpolate_history_arrays(
        values.to_numpy(dtype=float),
        trusted_mask.to_numpy(dtype=bool),
        values.index.as_unit("ns").asi8,
        max_gap_hours,
    )
    left_frame = pd.DataFrame(index=values.index, columns=values.columns, dtype=object)
    right_frame = pd.DataFrame(index=values.index, columns=values.columns, dtype=object)
    for column in range(len(values.columns)):
        for row in np.flatnonzero(imputed[:, column]):
            left_frame.iat[row, column] = pd.Timestamp(
                left_donors[row, column], unit="ns", tz="UTC"
            )
            right_frame.iat[row, column] = pd.Timestamp(
                right_donors[row, column], unit="ns", tz="UTC"
            )
    return PreparedHistory(
        pd.DataFrame(filled, index=values.index, columns=values.columns),
        pd.DataFrame(imputed, index=values.index, columns=values.columns),
        left_frame,
        right_frame,
        bool(np.isfinite(filled).all()),
    )


def origin_is_observed(trusted_mask: pd.DataFrame, origin: pd.Timestamp) -> bool:
    """Cho biết mọi feature có đáng tin cậy tại đúng origin dự báo hay không."""
    return origin in trusted_mask.index and bool(trusted_mask.loc[origin].all())


@dataclass
class ScalerBundle:
    """Các scaler feature và target được khớp trên tập train cùng thứ tự feature cố định."""

    x: StandardScaler
    y: StandardScaler
    features: tuple = FEATURES


def fit_train_scalers(
    values: pd.DataFrame,
    trusted_mask: pd.DataFrame,
    train_start: pd.Timestamp,
    train_end_exclusive: pd.Timestamp,
) -> ScalerBundle:
    """Khớp scaler trên các hàng đáng tin cậy trong miền train đã duy nhất và lập lại chỉ mục.

    Trả về scaler feature và target. Thiếu dữ liệu để khớp hoặc sai thứ tự feature
    sẽ gây ValueError. Các hàng giữ lại và lịch sử đã bù không được dùng để khớp.
    """
    if tuple(values.columns) != FEATURES or tuple(trusted_mask.columns) != FEATURES:
        raise ValueError("Unexpected feature order")
    selected = (values.index >= train_start) & (values.index < train_end_exclusive)
    train_values = values.loc[selected]
    train_mask = trusted_mask.loc[selected]
    complete = train_mask.all(axis=1) & np.isfinite(train_values).all(axis=1)
    pm_valid = train_mask["pm25"] & np.isfinite(train_values["pm25"])
    if not complete.any() or not pm_valid.any():
        raise ValueError("No trusted training values for scaler")
    # Khớp trên hàng thay vì các cửa sổ chồng lấn giúp tránh gán trọng số lặp lại.
    feature_scaler = StandardScaler().fit(train_values.loc[complete].to_numpy())
    target_scaler = StandardScaler().fit(
        train_values.loc[pm_valid, ["pm25"]].to_numpy()
    )
    return ScalerBundle(feature_scaler, target_scaler)


def transform_windows(
    X_raw: np.ndarray, y_raw: np.ndarray, scalers: ScalerBundle
) -> tuple[np.ndarray, np.ndarray]:
    """Biến đổi lịch sử 24x3 và 24 target, từ chối đầu ra không hợp lệ hoặc không hữu hạn."""
    if (
        scalers.features != FEATURES
        or X_raw.shape[1:] != (LOOKBACK_HOURS, FEATURE_COUNT)
        or y_raw.shape != (len(X_raw), HORIZON_HOURS)
    ):
        raise ValueError("Invalid tensor shape or feature order")
    scaled_x = (
        scalers.x.transform(X_raw.reshape(-1, FEATURE_COUNT))
        .reshape(X_raw.shape)
        .astype(np.float32)
    )
    scaled_y = (
        scalers.y.transform(y_raw.reshape(-1, 1))
        .reshape(y_raw.shape)
        .astype(np.float32)
    )
    if not np.isfinite(scaled_x).all() or not np.isfinite(scaled_y).all():
        raise ValueError("Non-finite tensors")
    return scaled_x, scaled_y


def inverse_targets(y_scaled: np.ndarray, scalers: ScalerBundle) -> np.ndarray:
    """Trả target về đơn vị PM2.5 thực với hình dạng ban đầu."""
    targets = np.asarray(y_scaled)
    return scalers.y.inverse_transform(targets.reshape(-1, 1)).reshape(targets.shape)


def save_scalers(scalers: ScalerBundle, path: Path) -> None:
    """Ghi các tham số scaler thành JSON tại đường dẫn do bên gọi quản lý."""

    def attributes(scaler):
        return {
            name: np.asarray(getattr(scaler, name)).tolist()
            for name in ("mean_", "var_", "scale_", "n_samples_seen_")
        }

    data = {
        "features": list(scalers.features),
        "sklearn": sklearn.__version__,
        "x": attributes(scalers.x),
        "y": attributes(scalers.y),
    }
    Path(path).write_text(json.dumps(data, indent=2), encoding="utf-8")


def load_scalers(path: Path) -> ScalerBundle:
    """Khôi phục scaler từ JSON và từ chối thứ tự feature không tương thích."""
    data = json.loads(Path(path).read_text(encoding="utf-8"))
    if tuple(data["features"]) != FEATURES:
        raise ValueError("Unexpected feature order")

    def restore(attributes):
        scaler = StandardScaler()
        for name, value in attributes.items():
            setattr(scaler, name, np.asarray(value))
        scaler.n_features_in_ = len(scaler.mean_)
        return scaler

    return ScalerBundle(restore(data["x"]), restore(data["y"]))
