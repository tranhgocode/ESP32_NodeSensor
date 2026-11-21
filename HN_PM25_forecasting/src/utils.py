"""Shared preparation, experiment configuration and dataset/run validation."""

from dataclasses import asdict, dataclass
from decimal import Decimal
import hashlib
import json
import math
from pathlib import Path
import re

import pandas as pd
import yaml

FEATURES = ("pm25", "temperature", "humidity")
LOOKBACK_HOURS = 24
HORIZON_HOURS = 4


@dataclass(frozen=True)
class PreparationConfig:
    """Immutable settings, validated before the pipeline reads or writes files."""

    input_csv: Path = Path("data/Hanoi_aqi/hanoi_aqi_ml_ready_fixed.csv")
    processed: Path = Path("data/processed/pm25_24x4_complete_v1")
    reports: Path | None = None
    train_fraction: float = 0.70
    validation_fraction: float = 0.15
    test_fraction: float = 0.15
    shuffle: bool = False
    seed: int = 42
    features: tuple = FEATURES
    lookback: int = LOOKBACK_HOURS
    horizon: int = HORIZON_HOURS
    timezone: str = "UTC"
    start: str | None = None
    end_exclusive: str | None = None
    max_interpolation_gap_hours: int | None = None

    def __post_init__(self):
        for name in ("input_csv", "processed"):
            object.__setattr__(self, name, Path(getattr(self, name)).resolve())
        reports = (
            self.reports
            or self.processed.parent / "visualizations" / self.processed.name
        )
        object.__setattr__(self, "reports", Path(reports).resolve())
        validate_config(self)

    @property
    def fractions(self):
        return {
            "train": self.train_fraction,
            "val": self.validation_fraction,
            "test": self.test_fraction,
        }

    @property
    def archive_name(self):
        percentages = []
        for value in self.fractions.values():
            text = format(Decimal(str(value)) * 100, "f")
            percentages.append(text.rstrip("0").rstrip(".") if "." in text else text)
        return "splits_" + "_".join(percentages) + ".npz"


def validate_config(config: PreparationConfig) -> None:
    _validate_split_fractions(config.fractions.values())
    _validate_data_contract(config)
    _validate_preparation_options(config)


def _validate_split_fractions(fractions):
    fractions = tuple(fractions)
    if any(
        type(v) not in (int, float) or not math.isfinite(v) or not 0 < v < 1
        for v in fractions
    ) or not math.isclose(sum(fractions), 1, abs_tol=1e-9, rel_tol=0):
        raise ValueError("split fractions must be finite, positive and sum to 1")


def _validate_data_contract(config):
    if (
        config.features != FEATURES
        or type(config.lookback) is not int
        or config.lookback != LOOKBACK_HOURS
        or type(config.horizon) is not int
        or config.horizon != HORIZON_HOURS
        or config.timezone != "UTC"
    ):
        raise ValueError(
            "Contract requires UTC, feature order pm25/temperature/humidity and 24 -> 4"
        )


def _validate_preparation_options(config):
    if (
        type(config.shuffle) is not bool
        or type(config.seed) is not int
        or config.seed < 0
    ):
        raise ValueError("shuffle must be boolean and seed a nonnegative integer")
    gap = config.max_interpolation_gap_hours
    if gap is not None and (type(gap) is not int or gap < 0):
        raise ValueError("max_interpolation_gap_hours must be a nonnegative integer")
    start, end = parse_time_bound(config.start), parse_time_bound(config.end_exclusive)
    if start is not None and end is not None and start >= end:
        raise ValueError("Start must precede exclusive end")


def parse_time_bound(value):
    if value is None:
        return None
    try:
        result = pd.Timestamp(value)
        if result.tzinfo is None:
            raise ValueError("Time bound requires a timezone")
        result = result.tz_convert("UTC")
        if pd.isna(result) or result != result.floor("h"):
            raise ValueError("Time bound must be a whole UTC hour")
        return result
    except (ValueError, TypeError) as exc:
        raise ValueError(f"Invalid time bound: {value}") from exc


def load_config(path: Path) -> PreparationConfig:
    """Read YAML settings, the standalone preparation CLI does not require YAML."""
    path = Path(path).resolve()
    raw = _read_yaml_mapping(path)
    for section in ("paths", "data", "split", "training"):
        if section in raw and not isinstance(raw[section], dict):
            raise ValueError(f"{section} must be a mapping")
    return PreparationConfig(
        **_preparation_paths(raw.get("paths", {}), path.parent.parent),
        **_preparation_data(raw.get("data", {})),
        **_preparation_split(raw.get("split", {})),
    )


def _preparation_paths(paths, base):
    if any(not isinstance(value, str) or not value.strip() for value in paths.values()):
        raise ValueError("Configured paths must be nonempty strings")
    return {
        "input_csv": base / paths.get("input_csv", str(PreparationConfig.input_csv)),
        "processed": base / paths.get("processed", str(PreparationConfig.processed)),
        "reports": base / paths["reports"] if "reports" in paths else None,
    }


def _preparation_data(data):
    features = data.get("features", list(FEATURES))
    if not isinstance(features, list):
        raise ValueError("data.features must be a list")
    return {
        "features": tuple(features),
        "lookback": data.get("lookback", LOOKBACK_HOURS),
        "horizon": data.get("horizon", HORIZON_HOURS),
        "timezone": data.get("timezone", "UTC"),
        "start": data.get("start"),
        "end_exclusive": data.get("end_exclusive"),
    }


def _preparation_split(split):
    return {
        "train_fraction": split.get("train_ratio", split.get("train_fraction", 0.70)),
        "validation_fraction": split.get(
            "validation_ratio", split.get("validation_fraction", 0.15)
        ),
        "test_fraction": split.get("test_ratio", split.get("test_fraction", 0.15)),
        "shuffle": split.get("shuffle", False),
    }


def validate_output_paths(config: PreparationConfig) -> None:
    """Keep dataset/reports separate from each other and from the source CSV."""
    dataset, reports, source = config.processed, config.reports, config.input_csv

    def overlaps(a, b):
        return a == b or a in b.parents or b in a.parents

    if overlaps(dataset, reports) or any(
        overlaps(p, source) for p in (dataset, reports)
    ):
        raise ValueError("Output paths overlap input CSV or each other")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def write_json(path: Path, value) -> None:
    Path(path).write_text(
        json.dumps(value, indent=2, ensure_ascii=False, allow_nan=False),
        encoding="utf-8",
    )


MODEL_NAMES = ("lstm", "conv1d_gru", "se_lstm", "cnn_lstm")
SPLIT_NAMES = ("train", "val", "test")
# Default artifact names retained for existing imports. Fingerprints resolve the
# actual archive from summary.json, including non-default split percentages.
ARTIFACT_FILES = ("summary.json", "scalers.json", "splits_70_15_15.npz")


@dataclass(frozen=True)
class ModelConfig:
    """The model section of YAML, preserved for construction and snapshots."""

    name: str
    input_shape: tuple
    layers: tuple


@dataclass(frozen=True)
class TrainingConfig:
    seeds: tuple[int, ...] = (42, 52, 62)
    epochs: int = 100
    batch_size: int = 32
    learning_rate: float = 0.001
    amsgrad: bool = True
    patience: int = 15
    shuffle: bool = False
    loss: str = "mse"
    device: str = "cpu"


def read_json(path: Path) -> dict:
    data = json.loads(
        Path(path).read_text(encoding="utf-8"), parse_constant=_reject_json_constant
    )
    if not isinstance(data, dict):
        raise ValueError(f"Expected JSON object: {path}")
    return data


def _reject_json_constant(value):
    raise ValueError(f"Non-finite JSON constant: {value}")


def write_run_json(path: Path, value: dict, *, exclusive: bool = False) -> None:
    text = json.dumps(value, ensure_ascii=False, indent=2, allow_nan=False)
    path = Path(path)
    if exclusive:
        with path.open("x", encoding="utf-8") as output:
            output.write(text)
    else:
        temporary = path.with_suffix(path.suffix + ".tmp")
        temporary.write_text(text, encoding="utf-8")
        temporary.replace(path)


def load_experiment_config(common_path: Path, model_path: Path):
    prep = load_config(common_path)
    common = _read_yaml_mapping(common_path)
    model = _load_model_config(_read_yaml_mapping(model_path))
    training = _load_training_config(common.get("training", {}))
    return prep, model, training


def _read_yaml_mapping(path):
    try:
        raw = yaml.safe_load(Path(path).read_text(encoding="utf-8"))
    except yaml.YAMLError as exc:
        raise ValueError(f"Invalid YAML: {path}") from exc
    if raw is None:
        return {}
    if not isinstance(raw, dict):
        raise ValueError(f"Configuration must be a mapping: {path}")
    return raw


def _load_model_config(raw):
    if set(raw) != {"model"} or not isinstance(raw["model"], dict):
        raise ValueError("Model YAML requires only a model section")
    model = raw["model"]
    if set(model) != {"name", "input_shape", "layers"}:
        raise ValueError("model requires name, input_shape and layers")
    if model["name"] not in MODEL_NAMES:
        raise ValueError("Unsupported model name")
    _validate_model_shape(model["input_shape"])
    _validate_model_layers(model["layers"])
    return ModelConfig(
        model["name"], tuple(model["input_shape"]), tuple(model["layers"])
    )


def _validate_model_shape(shape):
    if not isinstance(shape, list) or len(shape) != 2:
        raise ValueError("Model input_shape must be [24,3] or [null,3]")
    steps, features = shape
    if (
        steps is not None
        and (type(steps) is not int or steps != LOOKBACK_HOURS)
        or type(features) is not int
        or features != len(FEATURES)
    ):
        raise ValueError("Model input_shape must match 24-hour, 3-feature histories")


def _validate_model_layers(layers):
    if not isinstance(layers, list) or not layers:
        raise ValueError("Model layers must be a nonempty list")
    for layer in layers:
        if (
            not isinstance(layer, dict)
            or not isinstance(layer.get("type"), str)
            or not layer["type"]
        ):
            raise ValueError("Each layer needs a type name")
    output = layers[-1]
    if (
        output["type"] != "Dense"
        or type(output.get("units")) is not int
        or output["units"] != HORIZON_HOURS
    ):
        raise ValueError("Final layer must be Dense with 4 output units")


def _load_training_config(raw) -> TrainingConfig:
    defaults = asdict(TrainingConfig())
    if not isinstance(raw, dict) or set(raw) - set(defaults):
        raise ValueError("Unknown training configuration")
    values = {**defaults, **raw}
    _validate_training_numbers(values)
    _validate_training_policy(values)
    values["seeds"] = _validate_seeds(values["seeds"])
    return TrainingConfig(**values)


def _validate_seeds(seeds) -> tuple[int, ...]:
    if (
        not isinstance(seeds, (list, tuple))
        or not seeds
        or any(type(seed) is not int or not 0 <= seed < 2**31 for seed in seeds)
        or len(set(seeds)) != len(seeds)
    ):
        raise ValueError("Seeds must be unique nonnegative integers below 2**31")
    return tuple(seeds)


def _validate_training_numbers(values):
    for key in ("epochs", "batch_size", "patience"):
        if type(values[key]) is not int or values[key] < 1:
            raise ValueError(f"{key} must be a positive integer")
    rate = values["learning_rate"]
    if type(rate) not in (int, float) or not math.isfinite(rate) or rate <= 0:
        raise ValueError("learning_rate must be finite and positive")


def _validate_training_policy(values):
    if values["loss"] not in ("mae", "mse"):
        raise ValueError("Training loss must be mae or mse")
    if values["shuffle"] is not False or values["amsgrad"] is not True:
        raise ValueError("Training requires shuffle=false and amsgrad=true")
    if values["device"] != "cpu":
        raise ValueError("Training supports CPU only")


def dataset_fingerprint(processed_dir: Path) -> str:
    """Hash the actual three artifacts without deserializing train/test arrays."""
    root = Path(processed_dir).resolve()
    files = _dataset_artifact_files(root)
    hashes = {name: sha256(root / name) for name in files}
    canonical = json.dumps(hashes, sort_keys=True, separators=(",", ":"))
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def _dataset_artifact_files(root):
    summary = read_json(root / "summary.json")
    name = summary.get("archive")
    percentage = r"[0-9]+(?:[.][0-9]+)?"
    if not isinstance(name, str) or not re.fullmatch(
        rf"splits_{percentage}_{percentage}_{percentage}[.]npz", name
    ):
        raise ValueError("Invalid archive filename in summary.json")
    files = ("summary.json", "scalers.json", name)
    for filename in files:
        path = root / filename
        if path.resolve().parent != root or not path.is_file():
            raise ValueError(f"Missing or unsafe dataset artifact: {filename}")
    return files


def validate_run_id(run_id: str) -> str:
    if not isinstance(run_id, str) or not re.fullmatch(
        r"[A-Za-z0-9][A-Za-z0-9_-]*", run_id
    ):
        raise ValueError(
            "run_id must be a single alphanumeric name with optional _ or -"
        )
    return run_id


def validate_locked_run(run_dir: Path, selection_path: Path) -> dict:
    """Validate the selected models and their configured seeds, not a fixed suite size."""
    run_dir = Path(run_dir).resolve()
    if len(run_dir.parents) < 4:
        raise ValueError("Invalid run directory")
    selection = read_json(selection_path)
    entries, identities = _selection_entries(selection)
    root = run_dir.parents[3]
    training: TrainingConfig | None = None
    matched = False
    for entry in entries:
        path, protocol = _validate_frozen_entry(root, selection, entry)
        if training is not None and protocol != training:
            raise ValueError("Frozen runs have different training protocols")
        training = protocol
        matched |= path == run_dir
    if training is None:
        raise ValueError("Selection needs completed runs")
    _validate_selection_membership(selection, identities, training.seeds)
    if not matched:
        raise ValueError("Run is not in frozen selection")
    return selection


def _selection_entries(selection):
    if selection.get("primary_metric") != "mae" or selection.get("direction") != "min":
        raise ValueError("Selection must minimize validation MAE")
    validate_run_id(selection.get("run_id"))
    entries = selection.get("runs")
    if not isinstance(entries, list) or not entries:
        raise ValueError("Selection needs completed runs")
    identities = [_entry_identity(entry) for entry in entries]
    if len(set(identities)) != len(identities):
        raise ValueError("Duplicate model/seed in selection")
    return entries, set(identities)


def _entry_identity(entry):
    if not isinstance(entry, dict) or entry.get("model_name") not in MODEL_NAMES:
        raise ValueError("Invalid model in frozen selection")
    seed = entry.get("seed")
    _validate_seeds([seed])
    return entry["model_name"], seed


def _validate_frozen_entry(root, selection, entry) -> tuple[Path, TrainingConfig]:
    name, seed = _entry_identity(entry)
    relative = Path("runs") / name / selection["run_id"] / f"seed_{seed}"
    if entry.get("run_dir") != relative.as_posix():
        raise ValueError("Invalid selection run path")
    path = (root / relative).resolve()
    if root not in path.parents:
        raise ValueError("Frozen run path leaves the project")
    meta = read_json(path / "run_metadata.json")
    _validate_frozen_metadata(path, meta, entry, selection)
    snapshot = read_json(path / "config_snapshot.json")
    if (
        not isinstance(snapshot.get("model"), dict)
        or snapshot["model"].get("name") != name
    ):
        raise ValueError("Frozen model snapshot does not match the selection")
    return path, _load_training_config(snapshot.get("training", {}))


def _validate_frozen_metadata(path, meta, entry, selection):
    if (
        meta.get("status") != "completed"
        or meta.get("smoke") is not False
        or meta.get("model_name") != entry["model_name"]
        or meta.get("seed") != entry["seed"]
    ):
        raise ValueError("Frozen run is incomplete or has the wrong identity")
    for filename, field in (
        ("best.keras", "checkpoint_sha256"),
        ("config_snapshot.json", "config_sha256"),
    ):
        if (
            not entry.get(field)
            or sha256(path / filename) != entry[field]
            or meta.get(field) != entry[field]
        ):
            raise ValueError("Frozen run checkpoint or configuration changed")
    fingerprint = selection.get("dataset_fingerprint")
    if (
        not isinstance(fingerprint, str)
        or not fingerprint
        or meta.get("dataset_fingerprint") != fingerprint
    ):
        raise ValueError("Frozen run dataset differs from selection")
    directory = meta.get("processed_dir")
    if (
        not isinstance(directory, str)
        or dataset_fingerprint(Path(directory)) != fingerprint
    ):
        raise ValueError("Frozen run dataset changed since training")


def _validate_selection_membership(selection, identities, seeds):
    models = selection.get("models", sorted({name for name, seed in identities}))
    if (
        not isinstance(models, list)
        or not models
        or len(set(models)) != len(models)
        or any(name not in MODEL_NAMES for name in models)
    ):
        raise ValueError("Invalid selected model list")
    expected = {(name, seed) for name in models for seed in seeds}
    if identities != expected:
        raise ValueError("Selection is missing a configured model/seed run")
    if "primary_model" in selection and selection["primary_model"] not in models:
        raise ValueError("Primary model is not in the frozen suite")
    if (
        "representative_seed" in selection
        and selection["representative_seed"] not in seeds
    ):
        raise ValueError("Representative seed is not in the frozen suite")
