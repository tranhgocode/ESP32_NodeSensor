"""Forecast metrics on physical labels, with explicit zero/constant conventions."""

import numpy as np
from .utils import HORIZON_HOURS

METRIC_NAMES = ("mae", "rmse", "smape_pct", "r2")


def _record(y, p):
    error = p - y
    r2, status = _r2_score(y, error)
    return {
        "mae": float(np.mean(np.abs(error))),
        "rmse": float(np.sqrt(np.mean(error**2))),
        "smape_pct": _smape(y, p, error),
        "r2": r2,
        "r2_status": status,
        "n_pairs": int(y.size),
    }


def _smape(y, p, error):
    denominator = np.abs(y) + np.abs(p)
    ratio = np.divide(
        np.abs(error), denominator, out=np.zeros_like(y), where=denominator > 0
    )
    return float(200 * np.mean(ratio))


def _r2_score(y, error):
    if y.size < 2:
        return None, "insufficient_samples"
    sst = float(np.sum((y - np.mean(y)) ** 2))
    if sst == 0:
        return None, "constant_target"
    return float(1 - np.sum(error**2) / sst), "ok"


def compute_forecast_metrics(y_true_raw: np.ndarray, y_pred_raw: np.ndarray) -> dict:
    y = np.asarray(y_true_raw, dtype=np.float64)
    p = np.asarray(y_pred_raw, dtype=np.float64)
    _validate_forecasts(y, p)
    return {
        "overall": _record(y.ravel(), p.ravel()),
        "per_horizon": [
            {"horizon": h + 1, **_record(y[:, h], p[:, h])}
            for h in range(HORIZON_HOURS)
        ],
        "negative_prediction_fraction": float(np.mean(p < 0)),
    }


def _validate_forecasts(y, p):
    if (
        y.ndim != 2
        or y.shape[1] != HORIZON_HOURS
        or not len(y)
        or p.shape != y.shape
        or not np.isfinite(y).all()
        or not np.isfinite(p).all()
    ):
        raise ValueError("Metrics require aligned nonempty finite (N,4) arrays")


def summarize_seeds(records: list[dict]) -> dict:
    result = {}
    for name in METRIC_NAMES:
        values = [r[name] for r in records if r[name] is not None]
        result[name + "_mean"] = float(np.mean(values)) if values else None
        result[name + "_std"] = (
            float(np.std(values, ddof=1)) if len(values) > 1 else None
        )
        result[name + "_valid_seed_count"] = len(values)
    return result
