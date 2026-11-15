from dataclasses import dataclass, replace
from pathlib import Path
import numpy as np
import pandas as pd
from .utils import (
    PreparationConfig,
    provenance_verified,
    sha256,
    write_json,
    FEATURES,
    validate_config,
    validate_output_paths,
    parse_time_bound,
)
from .preprocessing import (
    fit_train_scalers,
    transform_windows,
    save_scalers,
    interpolate_history_arrays,
)


@dataclass
class HourlyData:
    values: pd.DataFrame
    observed_mask: pd.DataFrame
    trusted_mask: pd.DataFrame
    source: pd.Series
    issues: pd.DataFrame
    summary: dict
    gaps: pd.DataFrame


@dataclass(frozen=True)
class SplitBounds:
    start: pd.Timestamp
    end_exclusive: pd.Timestamp


@dataclass
class WindowSet:
    X_raw: np.ndarray
    y_raw: np.ndarray
    origins: pd.DatetimeIndex
    target_times: np.ndarray
    imputed_mask: np.ndarray
    left_donor_times: np.ndarray
    right_donor_times: np.ndarray
    metadata: pd.DataFrame
    rejections: pd.DataFrame


class DuplicateConflictError(ValueError):
    """Lưu các dòng có cùng mốc nhưng nội dung mâu thuẫn."""

    def __init__(self, rows):
        super().__init__("Conflicting duplicate timestamps")
        self.rows = rows


def audit_hourly(path, config):
    validate_config(config)
    raw = pd.read_csv(path)
    input_rows = len(raw)
    timecol = "datetime" if "datetime" in raw.columns else "timestamp"
    if not set(FEATURES + ("source", timecol)).issubset(raw.columns):
        raise ValueError("Missing required columns")
    try:
        idx = pd.to_datetime(raw[timecol], utc=True, errors="raise", format="mixed")
    except Exception as e:
        raise ValueError("Invalid timestamp") from e
    if idx.isna().any():
        raise ValueError("Invalid timestamp")
    start = parse_time_bound(config.start)
    end = parse_time_bound(config.end_exclusive)
    selected = pd.Series(True, index=raw.index)
    if start is not None:
        selected &= idx >= start
    if end is not None:
        selected &= idx < end
    raw = raw.loc[selected].copy()
    idx = idx.loc[selected]
    selected_rows = len(raw)
    if raw.empty:
        raise ValueError("Empty input after time filter")
    if (
        (idx.dt.minute != 0)
        | (idx.dt.second != 0)
        | (idx.dt.microsecond != 0)
        | (idx.dt.nanosecond != 0)
    ).any():
        raise ValueError("Timestamps must be whole hours")
    raw[timecol] = idx
    dup = raw.duplicated(timecol, keep=False)
    if dup.any():
        conflicts = []
        for _, rows in raw.loc[dup].groupby(timecol):
            if len(rows.drop_duplicates()) != 1:
                changed = [
                    column
                    for column in rows.columns
                    if rows[column].nunique(dropna=False) > 1
                ]
                for _, row in rows.iterrows():
                    conflicts.append(
                        {**row.to_dict(), "conflicting_fields": ",".join(changed)}
                    )
        if conflicts:
            raise DuplicateConflictError(pd.DataFrame(conflicts))
    duplicate_count = int(raw.duplicated(timecol).sum())
    raw = raw.drop_duplicates(timecol).set_index(timecol).sort_index()
    if raw.empty:
        raise ValueError("Empty input")
    index = pd.date_range(raw.index.min(), raw.index.max(), freq="h", tz="UTC")
    source = raw["source"].reindex(index)
    original = raw[list(FEATURES)].reindex(index)
    numeric = original.apply(pd.to_numeric, errors="coerce").replace(
        [np.inf, -np.inf], np.nan
    )
    invalid = pd.DataFrame(False, index=index, columns=FEATURES)
    invalid["pm25"] = numeric.pm25 < 0
    invalid["humidity"] = (numeric.humidity < 0) | (numeric.humidity > 100)
    numeric = numeric.mask(invalid)
    observed = numeric.notna()
    candidate = observed & np.broadcast_to(
        source.isin(config.allowed_sources).to_numpy()[:, None], observed.shape
    )
    trusted = candidate if provenance_verified(config) else candidate & False
    missing_rows = ~index.isin(raw.index)
    groups = (
        pd.Series(missing_rows, index=index)
        != pd.Series(missing_rows, index=index).shift()
    ).cumsum()
    gaps = []
    for _, g in pd.Series(missing_rows, index=index).groupby(groups):
        if g.iloc[0]:
            gaps.append(
                {"start": g.index[0], "end": g.index[-1], "missing_hours": len(g)}
            )
    gaps = pd.DataFrame(gaps, columns=["start", "end", "missing_hours"])
    issues = []
    for col in FEATURES:
        bad = original[col].notna() & numeric[col].isna()
        for t in original.index[bad]:
            issues.append(
                {
                    "timestamp": t,
                    "feature": col,
                    "value": str(original.at[t, col]),
                    "reason": "invalid",
                }
            )
    for t in numeric.index[numeric.temperature < 0]:
        issues.append(
            {
                "timestamp": t,
                "feature": "temperature",
                "value": numeric.at[t, "temperature"],
                "reason": "negative_review",
            }
        )
    summary = {
        "input_rows": input_rows,
        "selected_rows": selected_rows,
        "excluded_rows": input_rows - selected_rows,
        "filter": {
            "start": start.isoformat() if start is not None else None,
            "end_exclusive": end.isoformat() if end is not None else None,
        },
        "unique_rows": len(raw),
        "duplicates_removed": duplicate_count,
        "expected_hours": len(index),
        "missing_timestamps": int(missing_rows.sum()),
        "gap_count": len(gaps),
        "observed_missing": {c: int((~observed[c]).sum()) for c in FEATURES},
        "source_counts": raw.source.fillna("<missing>").value_counts().to_dict(),
        "provenance_status": (
            "verified" if provenance_verified(config) else "provenance_unverified"
        ),
        "timezone": (
            "UTC assumed from source dictionary, verification required"
            if not provenance_verified(config)
            else "UTC verified by supplied evidence"
        ),
        "input_sha256": sha256(path),
        "start": str(index[0]),
        "end": str(index[-1]),
    }
    return HourlyData(
        numeric,
        observed,
        trusted,
        source,
        pd.DataFrame(issues, columns=["timestamp", "feature", "value", "reason"]),
        summary,
        gaps,
    )


def make_split_bounds(index, config):
    if len(index) == 0:
        raise ValueError("Empty index")
    n = len(index)
    start = index[0]
    b1 = start + pd.Timedelta(hours=int(n * config.train_fraction))
    b2 = start + pd.Timedelta(
        hours=int(n * (config.train_fraction + config.validation_fraction))
    )
    end = index[-1] + pd.Timedelta(hours=1)
    return {
        "train": SplitBounds(start, b1),
        "validation": SplitBounds(b1, b2),
        "test": SplitBounds(b2, end),
    }


def assign_split(origin, bounds, horizon=24):
    first = origin + pd.Timedelta(hours=1)
    last = origin + pd.Timedelta(hours=horizon)
    return next(
        (
            name
            for name, b in bounds.items()
            if b.start <= first and last < b.end_exclusive
        ),
        None,
    )


def build_windows(data, bounds, config):
    index = data.values.index
    v = data.values.to_numpy(dtype=float)
    mask = data.trusted_mask.to_numpy(dtype=bool) & np.isfinite(v)
    source = data.source
    n = len(index)
    outputs = {k: [] for k in bounds}
    rejected = []
    for i, t in enumerate(index):
        flags = {
            "insufficient_history": i < 23,
            "origin_unobserved": not mask[i].all(),
            "history_missing": False,
            "label_missing": i + 24 >= n or not mask[i + 1 : i + 25, 0].all(),
            "split_boundary": False,
        }
        split = assign_split(t, bounds, 24)
        flags["split_boundary"] = split is None
        x = imp = left_donors = right_donors = None
        if i >= 23:
            x, imp, left_donors, right_donors = interpolate_history_arrays(
                v[i - 23 : i + 1],
                mask[i - 23 : i + 1],
                index[i - 23 : i + 1].as_unit("ns").asi8,
                config.max_gap_hours,
            )
            flags["history_missing"] = not np.isfinite(x).all()
        reason = next((k for k, yes in flags.items() if yes), None)
        if reason:
            rejected.append({"origin": t, "reason": reason, **flags})
            continue
        outputs[split].append(
            (
                x,
                v[i + 1 : i + 25, 0].copy(),
                t,
                index[i + 1 : i + 25].as_unit("ns").asi8.copy(),
                imp,
                left_donors,
                right_donors,
                str(source.iloc[i]),
                int(imp.sum()),
            )
        )
    rej = pd.DataFrame(
        rejected,
        columns=[
            "origin",
            "reason",
            "insufficient_history",
            "origin_unobserved",
            "history_missing",
            "label_missing",
            "split_boundary",
        ],
    )
    result = {}
    for split, rows in outputs.items():
        result[split] = WindowSet(
            np.array([r[0] for r in rows], dtype=float).reshape(-1, 24, 3),
            np.array([r[1] for r in rows], dtype=float).reshape(-1, 24),
            pd.DatetimeIndex([r[2] for r in rows]),
            np.array([r[3] for r in rows], dtype=np.int64).reshape(-1, 24),
            np.array([r[4] for r in rows], dtype=bool).reshape(-1, 24, 3),
            np.array([r[5] for r in rows], dtype=np.int64).reshape(-1, 24, 3),
            np.array([r[6] for r in rows], dtype=np.int64).reshape(-1, 24, 3),
            pd.DataFrame(
                [
                    {
                        "origin": r[2],
                        "split": split,
                        "source": r[7],
                        "imputed_count": r[8],
                    }
                    for r in rows
                ],
                columns=["origin", "split", "source", "imputed_count"],
            ),
            rej,
        )
    return result


@dataclass
class PreparationResult:
    status: str
    report_path: Path
    manifest_path: Path | None
    output_paths: list


def run_preparation(config: PreparationConfig, mode: str) -> PreparationResult:
    import json, platform, shutil, uuid
    import sklearn
    from . import plots
    import matplotlib.pyplot as plt

    if mode not in ("audit", "prepare"):
        raise ValueError("Mode must be audit or prepare")
    validate_config(config)
    validate_output_paths(config)
    try:
        data = audit_hourly(config.input_csv, config)
    except DuplicateConflictError as exc:
        config.tables.mkdir(parents=True, exist_ok=True)
        exc.rows.to_csv(config.tables / "data_duplicate_conflicts.csv", index=False)
        write_json(
            config.tables / "data_audit_summary.json",
            {
                "status": "failed",
                "reason": str(exc),
                "input_sha256": sha256(config.input_csv),
                "warning": "Prior tables and figures are stale and do not describe this failed audit",
            },
        )
        raise
    bounds = make_split_bounds(data.values.index, config)
    unknown = (
        set(data.source.dropna().unique())
        - set(config.allowed_sources)
        - {"5-year", "5-year+OpenAQ"}
    )
    if mode == "prepare" and (not provenance_verified(config) or unknown):
        raise ValueError("Preparation requires verified provenance and known sources")
    # Các quan sát ứng viên chỉ dùng cho chẩn đoán, trusted mask thực sự không bao giờ bị thay đổi.
    candidate = data.observed_mask & np.broadcast_to(
        data.source.isin(config.allowed_sources).to_numpy()[:, None],
        data.observed_mask.shape,
    )
    diagnostic = replace(data, trusted_mask=candidate)
    diagnostic_windows = build_windows(diagnostic, bounds, config)
    nofill = build_windows(diagnostic, bounds, replace(config, max_gap_hours=0))
    config.tables.mkdir(parents=True, exist_ok=True)
    config.figures.mkdir(parents=True, exist_ok=True)
    counts = {k: len(w.origins) for k, w in diagnostic_windows.items()}
    baseline = {k: len(w.origins) for k, w in nofill.items()}
    rejections = next(iter(diagnostic_windows.values())).rejections
    summary = {
        **data.summary,
        "diagnostic_status": "provisional candidate-source counts, not verified observations",
        "split_bounds": {
            k: {"start": str(b.start), "end_exclusive": str(b.end_exclusive)}
            for k, b in bounds.items()
        },
        "missing_policy": {
            "method": config.missing_method,
            "max_gap_hours": config.max_gap_hours,
            "endpoints": "original trusted observations inside history",
        },
        "candidate_windows": counts,
        "candidate_without_fill": baseline,
        "rejections": rejections.reason.value_counts().to_dict(),
        "imputed_cells": {
            k: int(w.imputed_mask.sum()) for k, w in diagnostic_windows.items()
        },
    }
    report = config.tables / "data_audit_summary.json"
    write_json(report, summary)
    diagnostics = {
        **missingness_tables(data),
        **coverage_tables(data, bounds, diagnostic_windows, nofill),
    }
    for name, table in diagnostics.items():
        table.to_csv(config.tables / (name + ".csv"), index=False)
    data.gaps.to_csv(config.tables / "data_gaps.csv", index=False)
    data.issues.to_csv(config.tables / "data_issues.csv", index=False)
    rejections.to_csv(config.tables / "data_rejections.csv", index=False)
    source_table = pd.DataFrame(
        {
            "source": data.source.fillna("<missing>"),
            "month": data.source.index.strftime("%Y-%m"),
            "hour_utc": data.source.index.hour,
        }
    )
    source_table.groupby(["month", "source"], dropna=False).size().rename(
        "hours"
    ).reset_index().to_csv(config.tables / "data_monthly_source.csv", index=False)
    source_table.groupby(["hour_utc", "source"], dropna=False).size().rename(
        "hours"
    ).reset_index().to_csv(config.tables / "data_hour_source.csv", index=False)
    transitions = data.source.fillna("<missing>")
    transitions = transitions[transitions.ne(transitions.shift())]
    transitions.rename("source").rename_axis("timestamp").reset_index().to_csv(
        config.tables / "data_source_transitions.csv", index=False
    )
    metadata = pd.concat(
        [w.metadata for w in diagnostic_windows.values()], ignore_index=True
    )
    if len(metadata):
        m = metadata.copy()
        m["month"] = pd.to_datetime(m.origin, utc=True).dt.strftime("%Y-%m")
        m.groupby(["split", "month", "source"], dropna=False).agg(
            windows=("origin", "size"), imputed_cells=("imputed_count", "sum")
        ).reset_index().to_csv(config.tables / "data_split_coverage.csv", index=False)
    else:
        pd.DataFrame(
            columns=["split", "month", "source", "windows", "imputed_cells"]
        ).to_csv(config.tables / "data_split_coverage.csv", index=False)
    for name, fig in [
        (
            "missingness",
            plots.plot_missingness(
                data.observed_mask, config.figures / "data_missingness.png"
            ),
        ),
        ("gaps", plots.plot_gap_histogram(data.gaps, config.figures / "data_gaps.png")),
        ("series", plots.plot_series(data, bounds, config.figures / "data_series.png")),
        (
            "train_distribution",
            plots.plot_train_distribution(
                data, bounds, config.figures / "data_train_distribution.png"
            ),
        ),
        (
            "source_series",
            plots.plot_source_series(
                data, bounds, config.figures / "data_source_series.png"
            ),
        ),
        (
            "coverage",
            plots.plot_coverage(
                diagnostics["data_coverage_month"], config.figures / "data_coverage.png"
            ),
        ),
        (
            "feature_gaps",
            plots.plot_feature_missing_histogram(
                diagnostics["data_feature_missing_histogram"],
                config.figures / "data_feature_missing_histogram.png",
            ),
        ),
    ]:
        plt.close(fig)
    if mode == "audit":
        return PreparationResult(
            data.summary["provenance_status"], report, None, [report]
        )
    windows = build_windows(data, bounds, config)
    if any(len(w.origins) == 0 for w in windows.values()):
        raise ValueError("All splits require at least one valid window")
    if config.processed.exists():
        raise ValueError(
            "Processed output already exists; preserve it and choose a new output path"
        )
    config.processed.parent.mkdir(parents=True, exist_ok=True)
    stage = config.processed.parent / (".stage_" + uuid.uuid4().hex)
    stage.mkdir()
    try:
        train = bounds["train"]
        scalers = fit_train_scalers(
            data.values, data.trusted_mask, train.start, train.end_exclusive
        )
        save_scalers(scalers, stage / "scalers.json")
        records = []
        for split, w in windows.items():
            X, y = transform_windows(w.X_raw, w.y_raw, scalers)
            np.savez_compressed(
                stage / (split + ".npz"),
                X=X,
                y=y,
                y_raw=w.y_raw,
                origins=w.origins.as_unit("ns").asi8,
                target_times=w.target_times,
                imputed_mask=w.imputed_mask,
                left_donor_times=w.left_donor_times,
                right_donor_times=w.right_donor_times,
            )
            for i, row in w.metadata.iterrows():
                records.append(
                    {
                        **row.to_dict(),
                        "npz_index": i,
                        "targets_epoch_ns": " ".join(map(str, w.target_times[i])),
                    }
                )
        pd.DataFrame(records).to_csv(stage / "window_metadata.csv", index=False)
        manifest = {
            "schema_version": "pm25_24x24_linear_v2",
            "status": "ready",
            "input_sha256": sha256(config.input_csv),
            "config_sha256": sha256(config.config_path) if config.config_path else None,
            "timezone": "UTC",
            "provenance": config.provenance,
            "feature_order": list(FEATURES),
            "lookback": 24,
            "horizon": 24,
            "label_units": "ug/m3",
            "source_policy": list(config.allowed_sources),
            "filter": {
                **data.summary["filter"],
                "selected_rows": data.summary["selected_rows"],
                "excluded_rows": data.summary["excluded_rows"],
                "actual_start": data.summary["start"],
                "actual_end": data.summary["end"],
            },
            "missing_policy": {
                "method": config.missing_method,
                "max_gap_hours": config.max_gap_hours,
                "endpoints": "original trusted observations inside history",
            },
            "split_bounds": summary["split_bounds"],
            "counts": {k: len(w.origins) for k, w in windows.items()},
            "rejections": summary["rejections"],
            "scaler_fit_range": {
                "start": str(train.start),
                "end_exclusive": str(train.end_exclusive),
            },
            "versions": {
                "python": platform.python_version(),
                "numpy": np.__version__,
                "pandas": pd.__version__,
                "sklearn": sklearn.__version__,
            },
            "timestamp_encoding": "UTC epoch nanoseconds, endpoint -1 means not imputed",
        }
        write_json(stage / "manifest.json", manifest)
        stage.rename(config.processed)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return PreparationResult(
        "ready", report, config.processed / "manifest.json", [config.processed]
    )


def missingness_tables(data: HourlyData) -> dict[str, pd.DataFrame]:
    """Thống kê chuỗi thiếu và tỷ lệ theo từng biến trên lưới giờ."""
    index = data.values.index
    absent = pd.Series(False, index=index)
    for gap in data.gaps.itertuples():
        absent.loc[gap.start : gap.end] = True
    source = data.source.fillna("<source missing>").astype(str).copy()
    source.loc[absent] = "<timestamp absent>"
    tables = []
    runs = []
    for feature in FEATURES:
        missing = ~data.observed_mask[feature]
        groups = missing.ne(missing.shift()).cumsum()
        for _, group in missing.groupby(groups):
            if group.iloc[0]:
                runs.append(
                    {
                        "feature": feature,
                        "start": group.index[0],
                        "end": group.index[-1],
                        "missing_hours": len(group),
                        "absent_timestamp_hours": int(absent.loc[group.index].sum()),
                        "present_row_missing_hours": int(
                            (~absent.loc[group.index]).sum()
                        ),
                    }
                )
        local = index.tz_convert("Asia/Ho_Chi_Minh")
        tables.append(
            pd.DataFrame(
                {
                    "feature": feature,
                    "month_utc": index.strftime("%Y-%m"),
                    "local_hour_assumed_utc": local.hour,
                    "source": source.to_numpy(),
                    "missing": missing.to_numpy().astype(int),
                    "absent_timestamp": absent.to_numpy().astype(int),
                    "present_row_missing": (missing & ~absent).to_numpy().astype(int),
                }
            )
        )
    cells = pd.concat(tables, ignore_index=True)
    result = {}
    groupings = [
        ("month", ["feature", "month_utc"]),
        ("local_hour", ["feature", "local_hour_assumed_utc"]),
        ("source", ["feature", "source"]),
    ]
    for name, columns in groupings:
        table = (
            cells.groupby(columns, dropna=False)
            .agg(
                hour_positions=("missing", "size"),
                missing_cells=("missing", "sum"),
                absent_timestamp_cells=("absent_timestamp", "sum"),
                present_row_missing_cells=("present_row_missing", "sum"),
            )
            .reset_index()
        )
        table["missing_ratio"] = table.missing_cells / table.hour_positions
        result["data_missing_by_" + name] = table
    run_table = pd.DataFrame(
        runs,
        columns=[
            "feature",
            "start",
            "end",
            "missing_hours",
            "absent_timestamp_hours",
            "present_row_missing_hours",
        ],
    )
    result["data_feature_missing_runs"] = run_table
    buckets = ["1", "2-3", "4-6", "7-24", ">24"]
    rows = []
    for feature in FEATURES:
        lengths = run_table.loc[run_table.feature == feature, "missing_hours"]
        counts = np.histogram(lengths, bins=[1, 2, 4, 7, 25, np.inf])[0]
        rows.extend(
            {"feature": feature, "bucket_hours": bucket, "runs": int(count)}
            for bucket, count in zip(buckets, counts)
        )
    result["data_feature_missing_histogram"] = pd.DataFrame(rows)
    return result


def coverage_tables(
    data: HourlyData,
    bounds: dict[str, SplitBounds],
    windows: dict[str, WindowSet],
    baseline: dict[str, WindowSet],
) -> dict[str, pd.DataFrame]:
    """Thống kê miền nhãn, origin được giữ và mức độ điền theo tập."""
    month_rows = []
    source_rows = []
    split_rows = []
    block_rows = []
    for split, bound in bounds.items():
        index = data.values.index
        domain = index[(index >= bound.start) & (index < bound.end_exclusive)]
        metadata = windows[split].metadata.copy()
        baseline_metadata = baseline[split].metadata.copy()
        for frame in (metadata, baseline_metadata):
            frame["first_label_month"] = (
                pd.to_datetime(frame.origin, utc=True) + pd.Timedelta(hours=1)
            ).dt.strftime("%Y-%m")
        for month in sorted(set(domain.strftime("%Y-%m"))):
            hours = domain[domain.strftime("%Y-%m") == month]
            selected = metadata[metadata.first_label_month == month]
            no_fill = baseline_metadata[baseline_metadata.first_label_month == month]
            month_num = int(month[5:])
            season = (
                "DJF"
                if month_num in (12, 1, 2)
                else (
                    "MAM"
                    if month_num in (3, 4, 5)
                    else "JJA" if month_num in (6, 7, 8) else "SON"
                )
            )
            month_rows.append(
                {
                    "split": split,
                    "month": month,
                    "season": season,
                    "label_domain_hours": len(hours),
                    "retained_origins": len(selected),
                    "no_fill_origins": len(no_fill),
                    "imputed_windows": int((selected.imputed_count > 0).sum()),
                    "imputed_cells": int(selected.imputed_count.sum()),
                }
            )
            hourly_sources = data.source.reindex(hours).fillna("<missing>").astype(str)
            labels = (
                set(hourly_sources)
                | set(selected.source.astype(str))
                | set(no_fill.source.astype(str))
            )
            for source in sorted(labels):
                source_rows.append(
                    {
                        "split": split,
                        "month": month,
                        "source": source,
                        "label_domain_source_hours": int(
                            (hourly_sources == source).sum()
                        ),
                        "retained_origins": int((selected.source == source).sum()),
                        "no_fill_origins": int((no_fill.source == source).sum()),
                    }
                )
        count = len(metadata)
        imputed_cells = int(metadata.imputed_count.sum())
        imputed_windows = int((metadata.imputed_count > 0).sum())
        split_rows.append(
            {
                "split": split,
                "label_domain_hours": len(domain),
                "retained_origins": count,
                "no_fill_origins": len(baseline_metadata),
                "imputed_windows": imputed_windows,
                "imputed_window_ratio": imputed_windows / count if count else 0.0,
                "imputed_cells": imputed_cells,
                "imputed_cell_ratio": imputed_cells / (72 * count) if count else 0.0,
            }
        )
        origins = pd.DatetimeIndex(
            pd.to_datetime(metadata.origin, utc=True)
        ).sort_values()
        if len(origins):
            cuts = np.r_[
                0,
                np.flatnonzero(np.diff(origins.as_unit("ns").asi8) != 3600000000000)
                + 1,
                len(origins),
            ]
            for first, end in zip(cuts[:-1], cuts[1:]):
                block_rows.append(
                    {
                        "split": split,
                        "start": origins[first],
                        "end": origins[end - 1],
                        "length_hours": int(end - first),
                    }
                )
    months = pd.DataFrame(month_rows)
    seasons = months.groupby(["split", "season"], as_index=False)[
        [
            "label_domain_hours",
            "retained_origins",
            "no_fill_origins",
            "imputed_windows",
            "imputed_cells",
        ]
    ].sum()
    return {
        "data_coverage_month": months,
        "data_coverage_season": seasons,
        "data_coverage_source": pd.DataFrame(source_rows),
        "data_coverage_split": pd.DataFrame(split_rows),
        "data_coverage_blocks": pd.DataFrame(
            block_rows, columns=["split", "start", "end", "length_hours"]
        ),
    }
