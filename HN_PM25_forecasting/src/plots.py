"""Preparation reports and saved model-result plots, organized as small functions."""

from pathlib import Path
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from .utils import FEATURES, HORIZON_HOURS, LOOKBACK_HOURS

FEATURE_LABELS = {
    "pm25": "PM2.5 (\u00b5g/m\u00b3)",
    "temperature": "Temperature (\u00b0C)",
    "humidity": "Humidity (%)",
}
SPLIT_COLORS = {"train": "#368252", "val": "#d98219", "test": "#c64242"}


def plot_missingness(mask, output_path=None):
    """The input mask is True where an observation is present."""
    figure, axis = plt.subplots(figsize=(12, 2.7))
    axis.imshow(
        (~mask).to_numpy().T,
        aspect="auto",
        interpolation="nearest",
        cmap="Reds",
        vmin=0,
        vmax=1,
    )
    axis.set_yticks(range(len(mask.columns)), mask.columns)
    _label_hourly_axis(axis, mask.index)
    axis.set(title="Missing observations on hourly grid", xlabel="UTC date")
    return finish_figure(figure, output_path)


def _label_hourly_axis(axis, index):
    positions = np.unique(np.linspace(0, len(index) - 1, min(6, len(index)), dtype=int))
    axis.set_xticks(positions, [index[i].strftime("%Y-%m-%d") for i in positions])


def plot_series(data, bounds, output_path=None):
    figure, axes = plt.subplots(len(FEATURES), 1, figsize=(12, 7), sharex=True)
    for axis, feature in zip(axes, FEATURES):
        _draw_feature_series(axis, data.values[feature], bounds)
        axis.set_ylabel(FEATURE_LABELS[feature])
    axes[0].legend(loc="upper right")
    axes[0].set_title("Hourly observations and label split boundaries")
    axes[-1].set_xlabel("UTC date")
    figure.autofmt_xdate()
    return finish_figure(figure, output_path)


def _draw_feature_series(axis, values, bounds):
    for split, bound in bounds.items():
        selected = values.loc[
            (values.index >= bound.start) & (values.index < bound.end_exclusive)
        ]
        axis.plot(
            selected.index, selected, color=SPLIT_COLORS[split], lw=0.6, label=split
        )
    for bound in list(bounds.values())[1:]:
        axis.axvline(bound.start, color="black", ls="--", lw=0.7)


def plot_train_distribution(data, bounds, output_path=None):
    table = train_distribution_table(data, bounds)
    return _draw_train_distribution(table, output_path)


def _draw_train_distribution(table, output_path=None):
    figure, axes = plt.subplots(1, len(FEATURES), figsize=(11, 3.5))
    for axis, feature in zip(axes, FEATURES):
        bins = table.loc[table.feature == feature]
        axis.bar(
            bins.bin_left,
            bins["count"],
            width=bins.bin_right - bins.bin_left,
            align="edge",
        )
        axis.set(title=feature, xlabel="Value", ylabel="Train observations")
    figure.suptitle("Distribution of original train-hour observations")
    return finish_figure(figure, output_path)


def plot_coverage(months, output_path=None):
    figure, axis = plt.subplots(figsize=(12, 4))
    positions = np.arange(len(months))
    axis.bar(
        positions, months.candidate_origins, label="Candidate windows", color="#d9d9d9"
    )
    axis.bar(
        positions,
        months.retained_origins,
        label="Complete windows retained",
        color="#368252",
    )
    labels = months.split.astype(str) + " " + months.month.astype(str)
    axis.set_xticks(positions, labels, rotation=90)
    axis.set(
        ylabel="Forecast origins", title="Complete-window coverage by first-label month"
    )
    axis.legend()
    return finish_figure(figure, output_path)


def save_preparation_reports(data, bounds, windows, output_dir):
    """Calculate tables once, save four CSVs, then save the matching four PNGs."""
    output = Path(output_dir)
    tables = build_preparation_tables(data, bounds, windows)
    _save_tables(tables, output / "tables")
    _save_figures(data, bounds, tables, output / "figures")


def _save_tables(tables, output_dir):
    output_dir.mkdir(parents=True)
    for name, table in tables.items():
        path = output_dir / (name + ".csv")
        if name in ("data_series", "data_missingness"):
            table.to_csv(path, index_label="timestamp")
        else:
            table.to_csv(path, index=False)


def _save_figures(data, bounds, tables, output_dir):
    output_dir.mkdir(parents=True)
    drawings = {
        "data_series": lambda path: plot_series(data, bounds, path),
        "data_missingness": lambda path: plot_missingness(
            ~tables["data_missingness"], path
        ),
        "data_train_distribution": lambda path: _draw_train_distribution(
            tables["data_train_distribution"], path
        ),
        "data_coverage": lambda path: plot_coverage(tables["data_coverage"], path),
    }
    for name, draw in drawings.items():
        figure = draw(output_dir / (name + ".png"))
        plt.close(figure)


def finish_figure(figure, output_path=None):
    """Caller owns a returned figure, failed layout/saving closes it here."""
    try:
        figure.tight_layout()
        if output_path is not None:
            path = Path(output_path)
            path.parent.mkdir(parents=True, exist_ok=True)
            figure.savefig(path, dpi=130, bbox_inches="tight")
    except BaseException:
        plt.close(figure)
        raise
    return figure


HISTOGRAM_BINS = 35
COVERAGE_COLUMNS = ["split", "month", "candidate_origins", "retained_origins"]


def series_table(data, bounds):
    table = data.values.copy()
    table["split"] = ""
    for split, bound in bounds.items():
        selected = (table.index >= bound.start) & (table.index < bound.end_exclusive)
        table.loc[selected, "split"] = split
    return table


def train_distribution_table(data, bounds):
    """Original train-hour histograms, computed once for CSV and PNG."""
    bound = bounds["train"]
    train = data.values.loc[
        (data.values.index >= bound.start) & (data.values.index < bound.end_exclusive)
    ]
    rows = []
    for feature in FEATURES:
        counts, edges = np.histogram(train[feature].dropna(), bins=HISTOGRAM_BINS)
        for left, right, count in zip(edges[:-1], edges[1:], counts):
            rows.append(
                {
                    "feature": feature,
                    "bin_left": float(left),
                    "bin_right": float(right),
                    "count": int(count),
                }
            )
    return pd.DataFrame(rows)


def coverage_table(data, bounds, windows):
    """Compare time-eligible windows with retained complete windows per month."""
    origins = data.values.index[LOOKBACK_HOURS - 1 : len(data.values) - HORIZON_HOURS]
    frames = []
    for split, bound in bounds.items():
        candidates = _candidate_origins(origins, bound)
        frames.append(
            pd.DataFrame(
                {
                    "split": split,
                    "month": (candidates + pd.Timedelta(hours=1)).strftime("%Y-%m"),
                    "candidate_origins": np.ones(len(candidates), dtype=int),
                    "retained_origins": candidates.isin(windows[split].origins).astype(
                        int
                    ),
                }
            )
        )
    if not frames:
        return pd.DataFrame(columns=COVERAGE_COLUMNS)
    table = pd.concat(frames, ignore_index=True)
    return table.groupby(["split", "month"], as_index=False, sort=False).sum()


def _candidate_origins(origins, bound):
    first_labels = origins + pd.Timedelta(hours=1)
    last_labels = origins + pd.Timedelta(hours=HORIZON_HOURS)
    return origins[(first_labels >= bound.start) & (last_labels < bound.end_exclusive)]


def build_preparation_tables(data, bounds, windows):
    return {
        "data_series": series_table(data, bounds),
        "data_missingness": data.values.isna(),
        "data_train_distribution": train_distribution_table(data, bounds),
        "data_coverage": coverage_table(data, bounds, windows),
    }


# Saved model results still use the legacy 24-hour forecast format.
LEGACY_HORIZON_HOURS = 24
HOUR_NS = 3_600_000_000_000
MODEL_STYLES = {
    "conv1d_gru": ("#ff7f0e", "--", "Conv1D-GRU"),
    "lstm": ("#2ca02c", "-.", "LSTM"),
    "se_lstm": ("#d62728", ":", "SE-LSTM"),
}


def load_forecast_timeline(evaluations: dict, horizon_hours: int = 1) -> pd.DataFrame:
    _validate_horizon(horizon_hours)
    if not evaluations or "actual" in evaluations:
        raise ValueError("Provide model evaluations, actual is a reserved column")
    reference = identity = None
    frame = pd.DataFrame()
    for name, directory in evaluations.items():
        current_identity, arrays = _read_saved_forecast(Path(directory), name)
        if reference is None:
            reference, identity = arrays, current_identity
            times = pd.to_datetime(arrays[1][:, horizon_hours - 1], unit="ns", utc=True)
            index = pd.DatetimeIndex(times, name="target_time")
            frame = pd.DataFrame(
                {"actual": arrays[2][:, horizon_hours - 1]}, index=index
            )
        elif current_identity != identity or any(
            not np.array_equal(a, b) for a, b in zip(reference, arrays[:3])
        ):
            raise ValueError(
                "Models must share the same dataset, split, origins and physical labels"
            )
        frame[name] = arrays[3][:, horizon_hours - 1]
    if identity is None:
        raise ValueError("Provide at least one model evaluation")
    frame.attrs.update(split=identity[1], horizon_hours=horizon_hours)
    return frame


def _validate_horizon(horizon):
    if type(horizon) is not int or not 1 <= horizon <= LEGACY_HORIZON_HOURS:
        raise ValueError("horizon_hours must be an integer from 1 to 24")


def _read_saved_forecast(folder, name):
    from .utils import read_json, sha256

    metadata = read_json(folder / "metrics.json")
    prediction_path = folder / "predictions.npz"
    if sha256(prediction_path) != metadata.get("predictions_sha256"):
        raise ValueError(f"Prediction checksum mismatch: {name}")
    identity = (metadata.get("dataset_fingerprint"), metadata.get("split"))
    if not identity[0] or identity[1] not in ("validation", "test"):
        raise ValueError(f"Missing dataset identity or split: {name}")
    with np.load(prediction_path, allow_pickle=False) as saved:
        arrays = tuple(
            saved[key]
            for key in ("origins", "target_times", "y_true_raw", "y_pred_raw")
        )
    _validate_saved_forecast(arrays, name)
    return identity, arrays


def _validate_saved_forecast(arrays, name):
    origins, times, actual, forecast = arrays
    count = len(origins)
    shape = (count, LEGACY_HORIZON_HOURS)
    offsets = np.arange(1, LEGACY_HORIZON_HOURS + 1) * HOUR_NS
    valid = (
        count
        and origins.shape == (count,)
        and times.shape == shape
        and actual.shape == shape
        and forecast.shape == shape
        and origins.dtype == np.int64
        and times.dtype == np.int64
        and np.all(np.diff(origins) > 0)
        and not np.any(origins % HOUR_NS)
        and np.array_equal(times, origins[:, None] + offsets)
        and np.isfinite(actual).all()
        and np.isfinite(forecast).all()
    )
    if not valid:
        raise ValueError(f"Invalid saved forecast arrays: {name}")


def plot_forecast_timeline(
    series,
    split="validation",
    horizon_hours=1,
    window_days=14,
    start=None,
    output_path=None,
):
    _validate_timeline(series, split, horizon_hours, window_days)
    selected, window_start, window_end = _timeline_window(series, start, window_days)
    figure, axis = plt.subplots(figsize=(18, 5.5))
    _draw_timeline_lines(axis, selected)
    _style_timeline(
        axis, split, horizon_hours, window_days, start, window_start, window_end
    )
    return finish_figure(figure, output_path)


def _validate_timeline(series, split, horizon, days):
    _validate_horizon(horizon)
    if type(days) is not int or days < 1:
        raise ValueError("window_days must be a positive integer")
    if (
        not isinstance(series.index, pd.DatetimeIndex)
        or series.index.tz is None
        or not series.index.is_unique
        or not series.index.is_monotonic_increasing
        or series.empty
        or "actual" not in series
        or len(series.columns) < 2
    ):
        raise ValueError(
            "Timeline needs actual/model columns and unique sorted timezone-aware times"
        )
    if split not in ("validation", "test"):
        raise ValueError("split must be validation or test")
    if (
        series.attrs.get("split", split) != split
        or series.attrs.get("horizon_hours", horizon) != horizon
    ):
        raise ValueError("Plot labels must match the saved split and selected horizon")


def _timeline_window(series, start, days):
    frame = series.copy()
    frame.index = frame.index.tz_convert("UTC")
    first = frame.index.min() if start is None else pd.Timestamp(start)
    first = (
        first.tz_localize("UTC") if first.tzinfo is None else first.tz_convert("UTC")
    )
    if first != first.floor("h"):
        raise ValueError("Window start must be a whole UTC hour")
    end = first + pd.Timedelta(days=days)
    selected = frame.loc[(frame.index >= first) & (frame.index < end)]
    if selected.empty:
        raise ValueError("No saved predictions in the requested time window")
    hours = pd.date_range(first, end, freq="h", inclusive="left")
    return selected.reindex(hours), first, end


def _draw_timeline_lines(axis, selected):
    axis.plot(
        selected.index,
        selected.actual,
        color="#1f77b4",
        lw=1.6,
        label="Actual PM2.5",
        zorder=3,
    )
    for name in selected.columns.drop("actual"):
        color, linestyle, label = MODEL_STYLES.get(name, ("#9467bd", "--", str(name)))
        axis.plot(
            selected.index,
            selected[name],
            color=color,
            ls=linestyle,
            lw=1.25,
            label=label,
        )


def _style_timeline(axis, split, horizon, days, start, first, end):
    import matplotlib.dates as dates

    label = f"First {days} Days" if start is None else f"{days}-Day Window"
    title = (
        "ML Model Forecasts vs Actual PM2.5\n"
        f"{label} of {split.title()} Set | Hanoi | Horizon +{horizon} h"
    )
    axis.set_title(title, fontweight="bold", pad=15)
    axis.set(
        xlabel="Target date (UTC)", ylabel="PM2.5 (\u00b5g/m\u00b3)", xlim=(first, end)
    )
    axis.xaxis.set_major_locator(dates.DayLocator(interval=max(1, days // 7), tz="UTC"))
    axis.xaxis.set_major_formatter(dates.DateFormatter("%m-%d", tz="UTC"))
    axis.tick_params(axis="x", rotation=45)
    axis.grid(True, linestyle="--", linewidth=0.7, alpha=0.3)
    axis.spines[["top", "right"]].set_visible(False)
    axis.legend(loc="upper right", framealpha=0.95)


def plot_model_comparison(root, run_id, split, horizon_rows):
    output = Path(root) / "reports/figures" / f"models_{run_id}" / split
    output.mkdir(parents=True, exist_ok=True)
    _save_horizon_metrics(horizon_rows, output / "horizon_metrics.png")
    _save_learning_curves(root, run_id, split, output / "learning_and_forecast.png")


def _save_horizon_metrics(rows, path):
    figure, axes = plt.subplots(2, 2, figsize=(12, 7))
    try:
        for axis, key, title in zip(
            axes.flat,
            ("mae", "rmse", "smape_pct", "r2"),
            ("MAE (ug/m3)", "RMSE (ug/m3)", "SMAPE (%)", "R2"),
        ):
            for name, group in rows.groupby("model"):
                means = group.groupby("horizon")[key].mean()
                axis.plot(means.index, means, label=name)
            axis.set(title=title, xlabel="Forecast hour")
            axis.legend(fontsize=7)
        finish_figure(figure, path)
    finally:
        plt.close(figure)


def _save_learning_curves(root, run_id, split, path):
    from .utils import MODEL_NAMES

    figure, axes = plt.subplots(1, 2, figsize=(12, 4))
    try:
        for name in MODEL_NAMES:
            run = Path(root) / "runs" / name / run_id / "seed_42"
            _add_learning_curves(axes, run, split, name, name == MODEL_NAMES[0])
        axes[0].set(
            xlabel="Epoch", ylabel="MAE (ug/m3)", title="Seed 42 learning curves"
        )
        axes[1].set(
            xlabel="UTC target time",
            ylabel="PM2.5 (ug/m3)",
            title="First eligible forecast origin, seed 42",
        )
        axes[1].tick_params(axis="x", rotation=30)
        for axis in axes:
            axis.legend(fontsize=7)
        finish_figure(figure, path)
    finally:
        plt.close(figure)


def _add_learning_curves(axes, run, split, name, show_actual):
    history = pd.read_csv(run / "history.csv")
    axes[0].plot(history.epoch + 1, history.mae_ugm3, label=name + " train")
    axes[0].plot(
        history.epoch + 1,
        history.val_mae_ugm3,
        linestyle="--",
        label=name + " validation",
    )
    with np.load(
        run / "evaluation" / split / "predictions.npz", allow_pickle=False
    ) as saved:
        times = pd.to_datetime(saved["target_times"][0], utc=True)
        if show_actual:
            axes[1].plot(times, saved["y_true_raw"][0], color="black", label="Observed")
        axes[1].plot(times, saved["y_pred_raw"][0], label=name)
