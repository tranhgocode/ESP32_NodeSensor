"""Evaluate a PM2.5 checkpoint or plot its performance on the test set."""

import argparse
from dataclasses import replace
from pathlib import Path
import shutil
from typing import NamedTuple
import uuid

import numpy as np

from src.baselines import persistence_forecast, seasonal_naive_forecast
from src.data_pipeline import DataPreparationPipeline, load_prepared_split
from src.metrics import compute_forecast_metrics
from src.preprocessing import load_scalers, inverse_targets
from src.utils import (
    HORIZON_HOURS,
    dataset_fingerprint,
    load_config,
    read_json,
    sha256,
    write_run_json,
    validate_locked_run,
)

PROJECT_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_COMMON = PROJECT_ROOT / "configs" / "common.yaml"
DEFAULT_OUTPUT_DIR = PROJECT_ROOT / "visualizations"
CHECKPOINT_NAME = "best.keras"
NANOSECONDS_PER_HOUR = 3_600_000_000_000


class SplitForecast(NamedTuple):
    history: np.ndarray
    actual: np.ndarray
    predicted: np.ndarray
    predicted_scaled: np.ndarray
    origins: np.ndarray
    target_times: np.ndarray


def predict_batches(model, X: np.ndarray, batch_size: int = 32) -> np.ndarray:
    return np.concatenate(
        [
            model(X[i : i + batch_size], training=False).numpy()
            for i in range(0, len(X), batch_size)
        ]
    )


def _restore_windows(config, processed, split, data, scalers):
    """Recover timestamps and verify the row order against the saved split."""
    summary = read_json(processed / "summary.json")
    bounds = summary["bounds"]
    fractions = summary["fractions"]
    pipeline = DataPreparationPipeline(
        replace(
            config,
            start=bounds["train"]["start"],
            end_exclusive=bounds["test"]["end_exclusive"],
            train_fraction=fractions["train"],
            validation_fraction=fractions["val"],
            test_fraction=fractions["test"],
        )
    )
    hourly = pipeline.read_csv()
    name = "val" if split == "validation" else split
    windows = pipeline.build_windows(hourly, pipeline.split_time(hourly))[name]
    scaled_x, scaled_y = scalers.transform(windows.X_raw, windows.y_raw)
    if not np.array_equal(scaled_x, data.X) or not np.array_equal(scaled_y, data.y):
        raise ValueError(
            f"Source CSV does not match the prepared {split} split: {config.input_csv}"
        )
    return windows


def _predict_split(checkpoint, processed, split, config) -> SplitForecast:
    data = load_prepared_split(processed, split)
    scalers = load_scalers(processed / "scalers.json")
    windows = _restore_windows(config, processed, split, data, scalers)

    import tensorflow as tf

    with tf.device("/CPU:0"):
        model = tf.keras.models.load_model(checkpoint, compile=False)
        scaled = predict_batches(model, data.X)
    if scaled.shape != data.y.shape:
        raise ValueError(f"Checkpoint predicted shape {scaled.shape}, expected {data.y.shape}")

    origins = windows.origins.as_unit("ns").asi8
    offsets = np.arange(1, HORIZON_HOURS + 1) * NANOSECONDS_PER_HOUR
    return SplitForecast(
        windows.X_raw,
        windows.y_raw,
        inverse_targets(scaled, scalers),
        scaled,
        origins,
        origins[:, None] + offsets,
    )


def _resolve_checkpoint(path: Path) -> Path:
    path = path.expanduser().resolve()
    if path.is_dir() or path.name != CHECKPOINT_NAME:
        path = path / CHECKPOINT_NAME
    if not path.is_file():
        raise FileNotFoundError(f"Checkpoint not found: {path}")
    return path


def _checkpoint_dataset(checkpoint, default_processed):
    metadata_path = checkpoint.parent / "run_metadata.json"
    if not metadata_path.is_file():
        return default_processed

    metadata = read_json(metadata_path)
    processed = Path(metadata["processed_dir"])
    if not processed.is_dir():
        processed = default_processed
    if sha256(checkpoint) != metadata.get("checkpoint_sha256"):
        raise ValueError("Run checkpoint changed")
    if dataset_fingerprint(processed) != metadata.get("dataset_fingerprint"):
        raise ValueError("Dataset changed since training")
    return processed


def evaluate_checkpoint(
    checkpoint: Path,
    output_dir: Path = DEFAULT_OUTPUT_DIR,
    *,
    common_path: Path = DEFAULT_COMMON,
) -> Path:
    """Run test inference and save per-horizon figures."""
    checkpoint = _resolve_checkpoint(checkpoint)
    config = load_config(common_path)
    processed = _checkpoint_dataset(checkpoint, config.processed)
    forecast = _predict_split(checkpoint, processed, "test", config)

    from src.evaluation_plots import save_test_figures

    output_dir = output_dir.expanduser().resolve()
    save_test_figures(
        forecast.actual, forecast.predicted, forecast.target_times, output_dir
    )
    return output_dir


def evaluate_run(
    run_dir: Path,
    split: str,
    selection_path: Path | None = None,
    *,
    common_path: Path = DEFAULT_COMMON,
) -> Path:
    run = Path(run_dir).resolve()
    meta = _validate_run(run, split, selection_path)
    processed = Path(meta["processed_dir"])
    forecast = _predict_split(
        run / CHECKPOINT_NAME, processed, split, load_config(common_path)
    )
    metrics = _compute_run_metrics(split, meta, forecast)
    return _save_run_evaluation(run, split, forecast, metrics)


def _validate_run(run, split, selection_path):
    if split not in ("validation", "test"):
        raise ValueError("Evaluation split must be validation or test")
    meta = read_json(run / "run_metadata.json")
    if meta.get("status") not in ("completed", "smoke_completed"):
        raise ValueError("Cannot evaluate an incomplete run")
    if split == "test":
        if selection_path is None:
            raise ValueError("Test evaluation requires frozen validation selection")
        validate_locked_run(run, selection_path)
    if sha256(run / CHECKPOINT_NAME) != meta.get("checkpoint_sha256") or sha256(
        run / "config_snapshot.json"
    ) != meta.get("config_sha256"):
        raise ValueError("Run checkpoint or config changed")
    processed = Path(meta["processed_dir"])
    if dataset_fingerprint(processed) != meta["dataset_fingerprint"]:
        raise ValueError("Dataset changed since training")
    return meta


def _compute_run_metrics(split, meta, forecast):
    return {
        "split": split,
        "smoke": meta["smoke"],
        "seed": meta["seed"],
        "dataset_fingerprint": meta["dataset_fingerprint"],
        "checkpoint_sha256": meta["checkpoint_sha256"],
        "config_sha256": meta["config_sha256"],
        "units": {
            "mae": "ug/m3",
            "rmse": "ug/m3",
            "smape_pct": "percent",
            "r2": "dimensionless",
        },
        "model": compute_forecast_metrics(forecast.actual, forecast.predicted),
        "baselines": {
            "persistence": compute_forecast_metrics(
                forecast.actual, persistence_forecast(forecast.history)
            ),
            "seasonal_naive": compute_forecast_metrics(
                forecast.actual, seasonal_naive_forecast(forecast.history)
            ),
        },
    }


def _save_run_evaluation(run, split, forecast, metrics):
    parent = run / "evaluation"
    parent.mkdir(exist_ok=True)
    target = parent / split
    if target.exists():
        raise FileExistsError(f"Evaluation already exists: {target}")
    stage = parent / (".stage_" + uuid.uuid4().hex)
    stage.mkdir()
    try:
        np.savez_compressed(
            stage / "predictions.npz",
            y_pred_scaled=forecast.predicted_scaled,
            y_pred_raw=forecast.predicted,
            y_true_raw=forecast.actual,
            origins=forecast.origins,
            target_times=forecast.target_times,
        )
        metrics["predictions_sha256"] = sha256(stage / "predictions.npz")
        write_run_json(stage / "metrics.json", metrics)
        stage.rename(target)
    finally:
        if stage.exists():
            shutil.rmtree(stage)
    return target / "metrics.json"


def _parse_args(argv):
    parser = argparse.ArgumentParser(description=__doc__)
    source = parser.add_mutually_exclusive_group(required=True)
    source.add_argument(
        "--run-dir", type=Path, help="Run directory for comparison metrics"
    )
    source.add_argument(
        "--checkpoint", type=Path, help="best.keras file or its parent directory"
    )
    parser.add_argument(
        "--split", choices=("validation", "test"), help="Required with --run-dir"
    )
    parser.add_argument("--selection", type=Path)
    parser.add_argument(
        "--output-dir", type=Path, help="Checkpoint figures, defaults to visualizations/"
    )
    parser.add_argument(
        "--common", type=Path, default=DEFAULT_COMMON,
        help="YAML with dataset and source CSV paths",
    )
    args = parser.parse_args(argv)
    if args.checkpoint is not None:
        if args.split == "validation" or args.selection is not None:
            parser.error("--checkpoint uses the test split directly; use --run-dir for comparison")
    elif args.split is None or args.output_dir is not None:
        parser.error("--run-dir requires --split; --output-dir is for --checkpoint")
    return parser, args


def main(argv=None):
    parser, args = _parse_args(argv)
    try:
        if args.checkpoint is not None:
            output = evaluate_checkpoint(
                args.checkpoint,
                args.output_dir or DEFAULT_OUTPUT_DIR,
                common_path=args.common,
            )
        else:
            output = evaluate_run(
                args.run_dir, args.split, args.selection, common_path=args.common
            )
        print(output)
    except (ValueError, OSError, RuntimeError) as exc:
        parser.exit(1, f"{exc}\n")


if __name__ == "__main__":
    main()
