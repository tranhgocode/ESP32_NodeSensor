"""Per-horizon diagnostic figures for a checkpoint's test predictions."""

from pathlib import Path
import shutil
from uuid import uuid4

import matplotlib.dates as mdates
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd

from .plots import FEATURE_LABELS, finish_figure
from .utils import HORIZON_HOURS

ERROR_UNIT = "\u00b5g/m\u00b3"
RESIDUAL_BINS = 30


def save_test_figures(actual, predicted, target_times, output_dir: Path) -> None:
    """Render all figures before publishing them to the requested directory."""
    output_dir.mkdir(parents=True, exist_ok=True)
    stage = output_dir / f".evaluation_{uuid4().hex}"
    stage.mkdir()
    try:
        for column in range(HORIZON_HOURS):
            horizon = column + 1
            frame = _horizon_frame(
                actual[:, column], predicted[:, column], target_times[:, column]
            )
            _save_horizon_figures(frame, horizon, stage / f"t+{horizon}")

        for image in sorted(stage.glob("*/*.png")):
            target = output_dir / image.relative_to(stage)
            target.parent.mkdir(exist_ok=True)
            image.replace(target)
    finally:
        shutil.rmtree(stage)


def _horizon_frame(actual, predicted, target_times):
    index = pd.to_datetime(target_times, unit="ns", utc=True)
    residual = predicted - actual
    return pd.DataFrame(
        {
            "actual": actual,
            "predicted": predicted,
            "residual": residual,
            "absolute_error": np.abs(residual),
        },
        index=index,
    ).asfreq("h")


def _save_horizon_figures(frame, horizon, output_dir):
    plots = (
        ("Actual_Predicted.png", "Actual vs. predicted PM2.5", _draw_actual_predicted),
        ("residual_distribution.png", "Residual distribution", _draw_residual_distribution),
        ("absolute_error_over_time.png", "Absolute error over time", _draw_absolute_error),
        ("error_by_pm25_range.png", "Observed vs. predicted PM2.5", _draw_observed_vs_predicted),
    )
    for filename, title, draw in plots:
        figure, axis = plt.subplots(figsize=(11, 4.5))
        try:
            draw(axis, frame)
            axis.set_title(f"{title} | Test set | t+{horizon}")
            axis.set_axisbelow(True)
            axis.grid(axis="y", alpha=0.25)
            finish_figure(figure, output_dir / filename)
        finally:
            plt.close(figure)


def _draw_actual_predicted(axis, frame):
    axis.plot(frame.index, frame.actual, ".-", label="Actual", lw=1.1, ms=2)
    axis.plot(frame.index, frame.predicted, ".-", label="Predicted", lw=1.1, ms=2)
    axis.set_ylabel(FEATURE_LABELS["pm25"])
    axis.legend()
    _format_target_time(axis)


def _draw_residual_distribution(axis, frame):
    axis.hist(frame.residual.dropna(), bins=RESIDUAL_BINS, color="#4c78a8")
    axis.axvline(0, color="black", linestyle="--", linewidth=1)
    axis.set(xlabel=f"Predicted − actual ({ERROR_UNIT})", ylabel="Forecast count")


def _draw_absolute_error(axis, frame):
    errors = frame.absolute_error.dropna()
    markers, stems, baseline = axis.stem(
        errors.index, errors, linefmt="C0-", markerfmt="C0o", basefmt="k-"
    )
    markers.set(markersize=3, markerfacecolor="none")
    stems.set(linewidth=0.7, alpha=0.7)
    baseline.set_linewidth(0.8)
    rmse = np.sqrt(np.mean(errors.to_numpy() ** 2))
    axis.set_title(f"RMSE = {rmse:.2f} {ERROR_UNIT}", loc="right")
    axis.set(ylabel=f"Absolute error ({ERROR_UNIT})", ylim=(0, None))
    _format_target_time(axis)


def _format_target_time(axis):
    locator = mdates.AutoDateLocator(tz="UTC", maxticks=8)
    axis.xaxis.set_major_locator(locator)
    axis.xaxis.set_major_formatter(mdates.ConciseDateFormatter(locator, tz="UTC"))
    axis.set_xlabel("Target time (UTC)")


def _draw_observed_vs_predicted(axis, frame):
    observations = frame[["actual", "predicted"]].dropna()
    axis.scatter(observations.actual, observations.predicted, s=12, alpha=0.65, linewidths=0)
    x_limits, y_limits = axis.get_xlim(), axis.get_ylim()
    limits = min(x_limits[0], y_limits[0]), max(x_limits[1], y_limits[1])
    axis.plot(limits, limits, color="#b74c4c", lw=1)
    axis.set(
        xlim=limits,
        ylim=limits,
        xlabel=f"Observed PM2.5 ({ERROR_UNIT})",
        ylabel=f"Predicted PM2.5 ({ERROR_UNIT})",
    )
    axis.set_aspect("equal", adjustable="box")
    axis.get_figure().set_size_inches(5.6, 5.6)
