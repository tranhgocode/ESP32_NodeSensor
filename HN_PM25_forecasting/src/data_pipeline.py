"""Read CSV -> split time -> build complete windows -> scale -> publish.

Intermediate dataclasses stay in memory, only the three data artifacts are
published. Diagnostic reports live outside the dataset directory.
"""

from dataclasses import dataclass
from decimal import Decimal
from pathlib import Path
import re
import shutil
from typing import NamedTuple, cast
import uuid
from zipfile import BadZipFile
import numpy as np
import pandas as pd
from .preprocessing import ScalerBundle
from .utils import (
    FEATURES,
    HORIZON_HOURS,
    LOOKBACK_HOURS,
    PreparationConfig,
    parse_time_bound,
    read_json,
    validate_output_paths,
    write_json,
)

SPLITS = ("train", "val", "test")
SCHEMA_VERSION = "pm25_24x4_complete_v1"


@dataclass
class HourlyData:
    values: pd.DataFrame
    summary: dict


@dataclass(frozen=True)
class SplitBounds:
    start: pd.Timestamp
    end_exclusive: pd.Timestamp


@dataclass
class WindowSet:
    X_raw: np.ndarray
    y_raw: np.ndarray
    origins: pd.DatetimeIndex


class DatasetSplit(NamedTuple):
    """Named access (split.X) or unpacking (X, y), owned by the caller."""

    X: np.ndarray
    y: np.ndarray


@dataclass(frozen=True)
class PreparationResult:
    output_dir: Path
    archive_path: Path
    reports_dir: Path
    counts: dict


class DataPreparationPipeline:
    """Facade, each method is one visible stage of the preparation flow."""

    def __init__(self, config: PreparationConfig):
        self.config = config

    def read_csv(self) -> HourlyData:
        raw = pd.read_csv(self.config.input_csv)
        time_column = "datetime" if "datetime" in raw else "timestamp"
        missing = set(FEATURES + (time_column,)) - set(raw.columns)
        if missing:
            raise ValueError("Missing required columns: " + ", ".join(sorted(missing)))
        input_rows = len(raw)
        dated = (
            raw[time_column]
            .astype("string")
            .str.match(r"^\d{4}[-/]\d{1,2}[-/]\d{1,2}(?:[T\s].*)?$")
            .fillna(False)
        )
        if not dated.all():
            raise ValueError(
                "Invalid timestamp, each row needs an explicit year-month-day date"
            )
        try:
            times = pd.to_datetime(
                raw[time_column], utc=True, errors="raise", format="mixed"
            )
        except (ValueError, TypeError) as exc:
            raise ValueError("Invalid timestamp in CSV") from exc
        if times.isna().any():
            raise ValueError("Invalid missing timestamp in CSV")
        if (times != times.dt.floor("h")).any():
            raise ValueError("Timestamps must be whole hours")
        assumed_utc = any(
            pd.Timestamp(value).tzinfo is None for value in raw[time_column]
        )
        raw[time_column] = times
        duplicates = raw.loc[raw.duplicated(time_column, keep=False)]
        for timestamp, rows in duplicates.groupby(time_column):
            if len(rows.drop_duplicates()) != 1:
                # The grouping column contains UTC Timestamps from to_datetime.
                timestamp = cast(pd.Timestamp, timestamp)
                raise ValueError(
                    f"Conflicting duplicate timestamp: {timestamp.isoformat()}"
                )
        duplicate_count = int(raw.duplicated(time_column).sum())
        raw = raw.drop_duplicates(time_column)
        start = parse_time_bound(self.config.start)
        end = parse_time_bound(self.config.end_exclusive)
        if start is not None:
            raw = raw.loc[raw[time_column] >= start]
        if end is not None:
            raw = raw.loc[raw[time_column] < end]
        if raw.empty:
            raise ValueError("CSV contains no rows in the requested time range")
        raw = raw.set_index(time_column).sort_index()
        grid = pd.date_range(raw.index[0], raw.index[-1], freq="h", tz="UTC")
        values = raw.loc[:, list(FEATURES)].apply(pd.to_numeric, errors="coerce")
        values = values.reindex(grid).astype(float).replace([np.inf, -np.inf], np.nan)
        values.loc[values.pm25 < 0, "pm25"] = np.nan
        values.loc[~values.humidity.between(0, 100), "humidity"] = np.nan
        return HourlyData(
            values,
            {
                "input_rows": input_rows,
                "unique_rows": len(raw),
                "duplicates_removed": duplicate_count,
                "hourly_grid_rows": len(grid),
                "missing_timestamps": int((~grid.isin(raw.index)).sum()),
                "naive_timestamps_assumed_utc": assumed_utc,
            },
        )

    def split_time(self, data: HourlyData) -> dict[str, SplitBounds]:
        # read_csv reindexes values onto a complete hourly DatetimeIndex.
        index = cast(pd.DatetimeIndex, data.values.index)
        # Decimal avoids losing an hour when .7 * 90 becomes 62.99999999999999.
        train_ratio = Decimal(str(self.config.train_fraction))
        val_ratio = Decimal(str(self.config.validation_fraction))
        train_end = int(len(index) * train_ratio)
        val_end = int(len(index) * (train_ratio + val_ratio))
        cuts = (0, train_end, val_end, len(index))
        start = index[0]
        return {
            name: SplitBounds(
                start + pd.Timedelta(hours=left), start + pd.Timedelta(hours=right)
            )
            for name, left, right in zip(SPLITS, cuts, cuts[1:])
        }

    @staticmethod
    def label_split(origin, bounds):
        first = origin + pd.Timedelta(hours=1)
        last = origin + pd.Timedelta(hours=HORIZON_HOURS)
        for name, bound in bounds.items():
            if bound.start <= first and last < bound.end_exclusive:
                return name
        return None

    def build_windows(self, data: HourlyData, bounds) -> dict[str, WindowSet]:
        values = data.values.to_numpy(dtype=float)
        index = data.values.index
        rows = {name: [] for name in SPLITS}
        # Each i is a forecast origin, history includes i, labels start at i+1.
        for i in range(LOOKBACK_HOURS - 1, len(values) - HORIZON_HOURS):
            split = self.label_split(index[i], bounds)
            if split is None:
                continue
            X = values[i - LOOKBACK_HOURS + 1 : i + 1]
            y = values[i + 1 : i + HORIZON_HOURS + 1, 0]
            if np.isfinite(X).all() and np.isfinite(y).all():
                rows[split].append((X, y, index[i]))
        windows = {}
        for name, samples in rows.items():
            X = np.asarray([s[0] for s in samples], dtype=float).reshape(-1, 24, 3)
            y = np.asarray([s[1] for s in samples], dtype=float).reshape(-1, 4)
            origins = pd.DatetimeIndex([s[2] for s in samples], tz="UTC")
            if name == "train" and self.config.shuffle:
                order = np.random.default_rng(self.config.seed).permutation(len(y))
                X, y, origins = X[order], y[order], origins[order]
            windows[name] = WindowSet(X, y, origins)
        return windows

    def run(self) -> PreparationResult:
        validate_output_paths(self.config)
        # PreparationConfig resolves the optional reports path in __post_init__.
        reports = cast(Path, self.config.reports)
        store = PreparedDatasetStore(self.config.processed)
        store.check_destination()
        if reports.exists():
            raise ValueError(f"Report output already exists: {reports}")
        data = self.read_csv()
        bounds = self.split_time(data)
        windows = self.build_windows(data, bounds)
        for name, window in windows.items():
            if len(window.y_raw) == 0:
                raise ValueError(f"{name}: no valid windows, split is empty")
        scalers = ScalerBundle.fit(data.values, bounds["train"])
        from .plots import save_preparation_reports

        reports.parent.mkdir(parents=True, exist_ok=True)
        stage = reports.parent / (".reports_" + uuid.uuid4().hex)
        stage.mkdir()
        try:
            save_preparation_reports(data, bounds, windows, stage)
            stage.rename(reports)
            try:
                summary = store.save(self.config, data, bounds, windows, scalers)
            except BaseException:
                # Roll back only the report directory published by this run.
                reports.rename(stage)
                raise
        finally:
            if stage.exists():
                shutil.rmtree(stage)
        return PreparationResult(
            self.config.processed,
            self.config.processed / summary["archive"],
            reports,
            summary["counts"],
        )


class PreparedDatasetStore:
    """Own the three-file format, callers do not need to know NPZ key names."""

    def __init__(self, output_dir: Path):
        self.output_dir = Path(output_dir).resolve()

    def check_destination(self):
        if self.output_dir.exists():
            raise ValueError(f"Dataset output already exists: {self.output_dir}")

    def save(self, config, data, bounds, windows, scalers) -> dict:
        self.check_destination()
        arrays = {}
        for name in SPLITS:
            window = windows[name]
            if len(window.y_raw) == 0:
                raise ValueError(f"{name}: no valid windows, split is empty")
            arrays[f"X_{name}"], arrays[f"y_{name}"] = scalers.transform(
                window.X_raw, window.y_raw
            )
        summary = {
            "schema_version": SCHEMA_VERSION,
            "feature_order": list(FEATURES),
            "target": "pm25",
            "units": "ug/m3",
            "lookback": LOOKBACK_HOURS,
            "horizon": HORIZON_HOURS,
            "timezone": "UTC",
            "archive": config.archive_name,
            "fractions": config.fractions,
            "bounds": {
                k: {
                    "start": b.start.isoformat(),
                    "end_exclusive": b.end_exclusive.isoformat(),
                }
                for k, b in bounds.items()
            },
            "counts": {name: len(windows[name].y_raw) for name in SPLITS},
            "shuffle_train": config.shuffle,
            "seed": config.seed,
            "missing_policy": "drop_incomplete_window",
            "input": data.summary,
            "reports_dir": str(config.reports),
        }
        self.output_dir.parent.mkdir(parents=True, exist_ok=True)
        stage = self.output_dir.parent / (".dataset_" + uuid.uuid4().hex)
        stage.mkdir()
        try:
            np.savez_compressed(stage / config.archive_name, **arrays)
            scalers.save(stage / "scalers.json")
            write_json(stage / "summary.json", summary)
            stage.rename(self.output_dir)
        finally:
            if stage.exists():
                shutil.rmtree(stage)
        return summary

    def load(self, split: str) -> DatasetSplit:
        name = "val" if split == "validation" else split
        if name not in SPLITS:
            raise ValueError(f"{split}: unknown split, choose train, val or test")
        try:
            return self._load(name)
        except (
            ValueError,
            OSError,
            KeyError,
            TypeError,
            OverflowError,
            EOFError,
            BadZipFile,
        ) as exc:
            raise ValueError(f"{name}: {exc}") from exc

    def _load(self, name) -> DatasetSplit:
        for filename in ("summary.json", "scalers.json"):
            if not (self.output_dir / filename).is_file():
                raise ValueError(f"Missing file {filename}")
        summary = read_json(self.output_dir / "summary.json")
        expected = {
            "schema_version": SCHEMA_VERSION,
            "feature_order": list(FEATURES),
            "target": "pm25",
            "units": "ug/m3",
            "lookback": 24,
            "horizon": 4,
            "timezone": "UTC",
            "missing_policy": "drop_incomplete_window",
        }
        if any(summary.get(key) != value for key, value in expected.items()):
            raise ValueError("summary.json violates the 24 -> 4 dataset contract")
        self._validate_summary(summary)
        filename = summary.get("archive")
        percentage = r"[0-9]+(?:[.][0-9]+)?"
        if not isinstance(filename, str) or not re.fullmatch(
            rf"splits_{percentage}_{percentage}_{percentage}[.]npz", filename
        ):
            raise ValueError("Invalid archive filename in summary.json")
        archive = self.output_dir / filename
        if not archive.is_file():
            raise ValueError(f"Missing file {filename}")
        if archive.resolve().parent != self.output_dir:
            raise ValueError("Archive must stay inside the dataset directory")
        ScalerBundle.load(self.output_dir / "scalers.json")
        counts = summary.get("counts")
        if not isinstance(counts, dict):
            raise ValueError("Missing sample counts in summary.json")
        with np.load(archive, allow_pickle=False) as saved:
            keys = {f"{array}_{split}" for array in ("X", "y") for split in SPLITS}
            if len(saved.files) != len(keys) or set(saved.files) != keys:
                raise ValueError("Archive must contain exactly six X/y split keys")
            X, y = saved[f"X_{name}"], saved[f"y_{name}"]
        if not isinstance(X, np.ndarray) or not isinstance(y, np.ndarray):
            raise ValueError("Archive X/y members must be NumPy arrays")
        n = counts.get(name)
        if type(n) is not int or n <= 0:
            raise ValueError("Invalid or empty split sample count")
        if X.shape != (n, 24, 3) or y.shape != (n, 4):
            raise ValueError("Wrong shape or count, expected X=(N,24,3) and y=(N,4)")
        if X.dtype != np.float32 or y.dtype != np.float32:
            raise ValueError("X and y must have dtype float32")
        if not np.isfinite(X).all() or not np.isfinite(y).all():
            raise ValueError("X and y must contain only finite values")
        return DatasetSplit(X, y)

    @staticmethod
    def _validate_summary(summary):
        """Validate the time split and settings recorded with the six arrays."""
        required = {
            "fractions",
            "bounds",
            "counts",
            "shuffle_train",
            "seed",
            "input",
            "reports_dir",
        }
        missing = required - set(summary)
        if missing:
            raise ValueError(
                "summary.json missing fields: " + ", ".join(sorted(missing))
            )
        fractions = summary["fractions"]
        if not isinstance(fractions, dict) or set(fractions) != set(SPLITS):
            raise ValueError("summary.json needs fractions for train/val/test")
        config = PreparationConfig(
            train_fraction=fractions["train"],
            validation_fraction=fractions["val"],
            test_fraction=fractions["test"],
            shuffle=summary["shuffle_train"],
            seed=summary["seed"],
        )
        if summary.get("archive") != config.archive_name:
            raise ValueError("Archive filename does not match split fractions")
        if (
            not isinstance(summary["reports_dir"], str)
            or not summary["reports_dir"].strip()
        ):
            raise ValueError("Invalid reports_dir in summary.json")
        counts, bounds, stats = summary["counts"], summary["bounds"], summary["input"]
        if (
            not isinstance(counts, dict)
            or not isinstance(bounds, dict)
            or not isinstance(stats, dict)
        ):
            raise ValueError("summary.json counts, bounds and input must be objects")
        for key in (
            "input_rows",
            "unique_rows",
            "duplicates_removed",
            "hourly_grid_rows",
            "missing_timestamps",
        ):
            if type(stats.get(key)) is not int or stats[key] < 0:
                raise ValueError(f"Invalid input statistic: {key}")
        if (
            type(stats.get("naive_timestamps_assumed_utc")) is not bool
            or stats["unique_rows"] + stats["duplicates_removed"] > stats["input_rows"]
            or stats["unique_rows"] + stats["missing_timestamps"]
            != stats["hourly_grid_rows"]
        ):
            raise ValueError("Inconsistent input statistics in summary.json")
        n_hours = stats["hourly_grid_rows"]
        train_ratio = Decimal(str(config.train_fraction))
        val_ratio = Decimal(str(config.validation_fraction))
        cuts = (
            0,
            int(n_hours * train_ratio),
            int(n_hours * (train_ratio + val_ratio)),
            n_hours,
        )
        first_start = None
        for split, left, right in zip(SPLITS, cuts, cuts[1:]):
            bound = bounds.get(split)
            if not isinstance(bound, dict):
                raise ValueError(f"Missing bounds for {split}")
            start = parse_time_bound(bound.get("start"))
            end = parse_time_bound(bound.get("end_exclusive"))
            if start is None or end is None or start >= end:
                raise ValueError(f"Invalid bounds for {split}")
            if first_start is None:
                first_start = start
            if start != first_start + pd.Timedelta(
                hours=left
            ) or end != first_start + pd.Timedelta(hours=right):
                raise ValueError(f"Bounds for {split} do not match the split fractions")
            count = counts.get(split)
            if type(count) is not int or not 0 < count <= right - left:
                raise ValueError(f"Invalid or empty sample count for {split}")


def load_prepared_split(output_dir: Path, split: str) -> DatasetSplit:
    return PreparedDatasetStore(output_dir).load(split)


def run_preparation(config: PreparationConfig) -> PreparationResult:
    return DataPreparationPipeline(config).run()
