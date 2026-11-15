"""Các hình phục vụ kiểm tra có thể được trả về cho bên gọi và tùy chọn lưu dưới dạng PNG."""

from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np


def _finish(fig, output_path):
    """Hoàn thiện bố cục và để bên gọi chịu trách nhiệm quản lý Figure."""
    fig.tight_layout()
    if output_path is not None:
        Path(output_path).parent.mkdir(parents=True, exist_ok=True)
        fig.savefig(output_path, dpi=130, bbox_inches="tight")
    return fig


def plot_missingness(mask, output_path=None):
    """Trả về Figure thể hiện dữ liệu thiếu, có thể tùy chọn lưu PNG tại output_path."""
    fig, ax = plt.subplots(figsize=(12, 2.7))
    ax.imshow((~mask).to_numpy().T, aspect="auto", interpolation="nearest", cmap="Reds")
    ax.set_yticks(range(len(mask.columns)), mask.columns)
    ax.set_title("Missing observations on hourly grid")
    ax.set_xlabel("UTC date (assumed until verified)")
    positions = np.unique(
        np.linspace(0, max(len(mask) - 1, 0), min(6, len(mask)), dtype=int)
    )
    ax.set_xticks(positions, [mask.index[i].strftime("%Y-%m-%d") for i in positions])
    return _finish(fig, output_path)


def plot_gap_histogram(gaps, output_path=None):
    """Trả về Figure biểu đồ phân bố khoảng trống thời gian và có thể tùy chọn lưu."""
    fig, ax = plt.subplots(figsize=(8, 3))
    bins = [1, 2, 4, 7, 25, float("inf")]
    counts = np.histogram(gaps.missing_hours, bins=bins)[0] if len(gaps) else [0] * 5
    ax.bar(["1", "2–3", "4–6", "7–24", ">24"], counts)
    ax.set(xlabel="Missing hours", ylabel="Gap count", title="Missing timestamp runs")
    return _finish(fig, output_path)


def plot_series(data, bounds, output_path=None):
    """Trả về ba chuỗi quan sát cùng ranh giới các tập dữ liệu, có thể tùy chọn lưu PNG."""
    fig, axes = plt.subplots(3, 1, figsize=(12, 6), sharex=True)
    labels = {
        "pm25": "PM2.5 (µg/m³)",
        "temperature": "Temperature (°C)",
        "humidity": "Humidity (%)",
    }
    split_colors = {"train": "#A8D5BA", "validation": "#FFD6A5", "test": "#FFADAD"}
    for axis, feature in zip(axes, data.values):
        for split, bound in bounds.items():
            values = data.values.loc[
                (data.values.index >= bound.start)
                & (data.values.index < bound.end_exclusive),
                feature,
            ]
            axis.plot(
                values.index,
                values,
                color=split_colors[split],
                lw=0.5,
                label="val" if split == "validation" else split,
            )
        axis.set_ylabel(labels[feature])
        for bound in list(bounds.values())[1:]:
            axis.axvline(bound.start, color="red", ls="--", lw=0.8)
    axes[0].legend(loc="upper right")
    axes[0].set_title("Observed values, gaps and label split boundaries")
    return _finish(fig, output_path)


def plot_train_distribution(data, bounds, output_path=None):
    """Trả về Figure phân bố chỉ từ tập train, có thể tùy chọn lưu PNG."""
    fig, axes = plt.subplots(1, 3, figsize=(10, 3))
    train = bounds["train"]
    values = data.values.loc[
        (data.values.index >= train.start) & (data.values.index < train.end_exclusive)
    ]
    for axis, feature in zip(axes, values):
        axis.hist(values[feature].dropna(), bins=35)
        axis.set_title(feature)
    fig.suptitle("Train observations, provisional")
    return _finish(fig, output_path)


def plot_source_series(data, bounds, output_path=None):
    """Trả về các quan sát PM2.5 được tô màu theo nguồn, có thể tùy chọn lưu PNG."""
    fig, ax = plt.subplots(figsize=(12, 3))
    for label in data.source.dropna().unique():
        values = data.values.pm25.where(data.source == label)
        ax.plot(
            values.index,
            values,
            linestyle="",
            marker=".",
            markersize=1,
            label=str(label),
        )
    for bound in list(bounds.values())[1:]:
        ax.axvline(bound.start, color="black", ls="--", lw=0.8)
    ax.set(title="PM2.5 observations by source (provisional)", ylabel="PM2.5 (µg/m³)")
    ax.legend(markerscale=4)
    return _finish(fig, output_path)


def plot_coverage(months, output_path=None):
    """Trả về mức độ bao phủ origin theo tháng ở dạng tạm tính, có thể tùy chọn lưu PNG."""
    fig, ax = plt.subplots(figsize=(12, 3))
    labels = months["split"].astype(str) + " " + months["month"].astype(str)
    ax.bar(
        np.arange(len(months)), months["retained_origins"], label="Nội suy tuyến tính"
    )
    ax.plot(
        np.arange(len(months)),
        months["no_fill_origins"],
        ".-",
        color="black",
        label="Không điền",
    )
    ax.set_xticks(np.arange(len(months)), labels, rotation=90)
    ax.set_ylabel("Retained origins")
    ax.set_title("Provisional origins by first-label month")
    ax.legend()
    return _finish(fig, output_path)


def plot_feature_missing_histogram(histogram, output_path=None):
    """Trả về số lượng các nhóm chuỗi thiếu theo feature và có thể tùy chọn lưu PNG."""
    fig, ax = plt.subplots(figsize=(8, 3))
    buckets = ["1", "2-3", "4-6", "7-24", ">24"]
    width = 0.25
    for position, feature in enumerate(("pm25", "temperature", "humidity")):
        counts = (
            histogram[histogram.feature == feature]
            .set_index("bucket_hours")
            .reindex(buckets)
            .runs.fillna(0)
        )
        ax.bar(
            np.arange(len(buckets)) + (position - 1) * width,
            counts,
            width,
            label=feature,
        )
    ax.set_xticks(range(len(buckets)), buckets)
    ax.set(
        xlabel="Consecutive missing hours",
        ylabel="Runs",
        title="Missing runs by feature",
    )
    ax.legend()
    return _finish(fig, output_path)
