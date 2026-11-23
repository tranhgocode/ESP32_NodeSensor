"""Train a PM2.5 forecasting model on CPU."""

import argparse
from dataclasses import asdict
import json
import os
from pathlib import Path
import platform
from time import perf_counter

import keras
from matplotlib.figure import Figure
from matplotlib.ticker import MaxNLocator
import numpy as np
import pandas as pd
import tensorflow as tf

from src.data_pipeline import load_prepared_split
from src.models import build_model
from src.preprocessing import load_scalers
from src.utils import (
    MODEL_NAMES,
    _load_training_config,
    dataset_fingerprint,
    load_experiment_config,
    sha256,
    validate_run_id,
    write_run_json,
)

EPOCH_METRICS = ("loss", "val_loss", "val_rmse")


def cpu_scope():
    return tf.device("/CPU:0")


def _compile_model(model, training):
    loss_class = (
        keras.losses.MeanSquaredError
        if training.loss == "mse"
        else keras.losses.MeanAbsoluteError
    )
    model.compile(
        loss=loss_class(),
        optimizer=keras.optimizers.Adam(
            learning_rate=training.learning_rate,
            amsgrad=training.amsgrad,
        ),
        metrics=[
            keras.metrics.MeanAbsoluteError(name="mae"),
            keras.metrics.RootMeanSquaredError(name="rmse"),
        ],
    )


def _batches(split, batch_size):
    options = tf.data.Options()
    options.threading.private_threadpool_size = 1
    dataset = tf.data.Dataset.from_tensor_slices((split.X, split.y))
    return dataset.batch(batch_size).with_options(options).prefetch(1)


def _fit_model(model, checkpoint, training, train, validation):
    callbacks = [
        keras.callbacks.ModelCheckpoint(
            checkpoint,
            monitor="val_loss",
            mode="min",
            save_best_only=True,
        ),
        keras.callbacks.EarlyStopping(
            monitor="val_loss",
            mode="min",
            patience=training.patience,
            restore_best_weights=True,
        ),
        keras.callbacks.TerminateOnNaN(),
    ]
    return model.fit(
        _batches(train, training.batch_size),
        validation_data=_batches(validation, training.batch_size),
        epochs=training.epochs,
        shuffle=training.shuffle,
        callbacks=callbacks,
        verbose=2,
    )


def _save_learning_curve(path, history, loss_name):
    epochs = history.index + 1
    figure = Figure(figsize=(10, 4), layout="constrained")
    loss, rmse = figure.subplots(1, 2)
    loss.plot(epochs, history["loss"], ".-", label="loss")
    loss.plot(epochs, history["val_loss"], ".-", label="val_loss")
    loss.set(
        title="Training and validation loss", ylabel=f"{loss_name.upper()} (scaled)"
    )
    rmse.plot(epochs, history["val_rmse"], ".-", label="val_rmse")
    rmse.set(title="Validation RMSE", ylabel="RMSE (scaled)")
    for axis in (loss, rmse):
        axis.set_xlabel("Epoch")
        axis.xaxis.set_major_locator(MaxNLocator(integer=True, min_n_ticks=1))
        axis.legend()
        axis.grid(alpha=0.25)
    figure.savefig(path, dpi=150)


def _save_run(run, history, snapshot, metadata):
    """Write the learning history and the files required by evaluation."""
    processed = Path(metadata["processed_dir"])
    if dataset_fingerprint(processed) != metadata["dataset_fingerprint"]:
        raise ValueError("Dataset changed during training")

    table = pd.DataFrame(history.history, index=pd.Index(history.epoch, name="epoch"))
    required = [*EPOCH_METRICS, "mae", "val_mae"]
    if (
        table.empty
        or not set(required).issubset(table)
        or not np.isfinite(table[required].to_numpy()).all()
    ):
        raise ValueError(
            "Training history is missing required metrics or contains non-finite values"
        )
    target_scale = load_scalers(processed / "scalers.json").y.scale_
    if target_scale is None:
        raise ValueError("PM2.5 target scaler is missing its scale")
    scale = target_scale[0]
    table["mae_ugm3"] = table["mae"] * scale
    table["val_mae_ugm3"] = table["val_mae"] * scale
    table.to_csv(run / "history.csv")
    _save_learning_curve(
        run / "learning_curve.png", table, snapshot["training"]["loss"]
    )

    snapshot_path = run / "config_snapshot.json"
    write_run_json(snapshot_path, snapshot)
    write_run_json(
        run / "run_metadata.json",
        {
            **metadata,
            "status": "completed",
            "config_sha256": sha256(snapshot_path),
            "checkpoint_sha256": sha256(run / "best.keras"),
        },
    )


def _train_seed(run, seed, model_config, training, train, validation):
    run.mkdir()
    print(f"Training {model_config['name']} with seed {seed}", flush=True)
    keras.backend.clear_session()
    keras.utils.set_random_seed(seed)
    started = perf_counter()
    with cpu_scope():
        model = build_model(model_config)
        _compile_model(model, training)
        model.summary()
        history = _fit_model(model, run / "best.keras", training, train, validation)
    return history, model.count_params(), perf_counter() - started


def _run_parent(
    project_root: Path,
    model_name: str,
    run_id: str | None,
    run_dir: Path | None,
    output_dir: Path | None,
) -> Path:
    """Resolve the experiment directory that contains the per-seed folders."""
    if (run_id is None) == (run_dir is None):
        raise ValueError("Choose exactly one of --name and --run_dir")

    root = project_root / "runs"
    if output_dir is not None:
        root = Path(os.path.abspath(output_dir.expanduser()))
    if run_dir is None:
        assert run_id is not None
        return root / model_name / validate_run_id(run_id)

    path = run_dir.expanduser()
    if output_dir is None:
        return Path(os.path.abspath(path))

    # A custom run path must stay inside the output root when both are set.
    parent = Path(os.path.abspath(root / path))
    if path.anchor or parent == root or not parent.is_relative_to(root):
        raise ValueError("--run_dir must be relative and inside --output_dir")
    return parent


def train_experiment(
    common_path: Path,
    model_path: Path,
    run_id: str | None = None,
    *,
    run_dir: Path | None = None,
    output_dir: Path | None = None,
    seed: int | None = None,
    epochs: int | None = None,
    batch_size: int | None = None,
    patience: int | None = None,
) -> list[Path]:
    common_path, model_path = Path(common_path).resolve(), Path(model_path).resolve()
    prep, config, defaults = load_experiment_config(common_path, model_path)
    overrides = {
        "seeds": None if seed is None else (seed,),
        "epochs": epochs,
        "batch_size": batch_size,
        "patience": patience,
    }
    settings = asdict(defaults)
    settings.update(
        {key: value for key, value in overrides.items() if value is not None}
    )
    training = _load_training_config(settings)

    parent = _run_parent(common_path.parent.parent, config.name, run_id, run_dir, output_dir)
    if parent.resolve() != parent:
        raise ValueError(f"Redirected run directory: {parent}")
    seed_runs = {seed: parent / f"seed_{seed}" for seed in training.seeds}
    for run in seed_runs.values():
        if run.exists() or run.is_symlink():
            raise FileExistsError(f"Seed run already exists: {run}")

    fingerprint = dataset_fingerprint(prep.processed)
    train = load_prepared_split(prep.processed, "train")
    validation = load_prepared_split(prep.processed, "validation")
    snapshot = {"model": asdict(config), "training": asdict(training)}
    arguments = {
        "common": str(common_path),
        "model": str(model_path),
        "name": parent.name,
        "run_dir": str(parent),
        **settings,
    }
    print(json.dumps(arguments, indent=2), flush=True)

    tf.config.experimental.enable_op_determinism()
    parent.mkdir(parents=True, exist_ok=True)
    for active_seed, run in seed_runs.items():
        history, parameter_count, seconds = _train_seed(
            run,
            active_seed,
            snapshot["model"],
            training,
            train,
            validation,
        )
        _save_run(
            run,
            history,
            snapshot,
            {
                "model_name": config.name,
                "seed": active_seed,
                "smoke": False,  # Required by evaluation and comparison of existing runs.
                "processed_dir": str(prep.processed),
                "dataset_fingerprint": fingerprint,
                "parameter_count": parameter_count,
                "training_seconds": seconds,
                "device_requested": training.device,
                "platform": platform.platform(),
            },
        )
    return list(seed_runs.values())


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--common", type=Path, required=True, help="Common YAML configuration"
    )
    parser.add_argument("--model", required=True, help="Model name or YAML path")
    destination = parser.add_mutually_exclusive_group(required=True)
    destination.add_argument("--name", help="Run name under <output_dir>/<model>")
    destination.add_argument(
        "--run_dir", "--run-dir", type=Path,
        help="Experiment directory, relative to --output_dir when provided",
    )
    parser.add_argument(
        "--output_dir", "--output-dir", type=Path,
        help="Root directory for training outputs, defaults to the project's runs directory with --name",
    )
    parser.add_argument("--seed", type=int, help="Seed for a single training run")
    parser.add_argument("--epochs", type=int, help="Maximum number of epochs")
    parser.add_argument("--batch-size", type=int, help="Samples per batch")
    parser.add_argument(
        "--patience", type=int, help="Epochs without improvement before stopping"
    )
    args = parser.parse_args(argv)

    model_path = Path(args.model)
    if args.model in MODEL_NAMES:
        model_path = args.common.parent / f"{args.model}.yaml"
    try:
        for run in train_experiment(
            args.common,
            model_path,
            args.name,
            run_dir=args.run_dir,
            output_dir=args.output_dir,
            seed=args.seed,
            epochs=args.epochs,
            batch_size=args.batch_size,
            patience=args.patience,
        ):
            print(run)
    except (ValueError, OSError, RuntimeError) as exc:
        parser.exit(1, f"{exc}\n")


if __name__ == "__main__":
    main()
