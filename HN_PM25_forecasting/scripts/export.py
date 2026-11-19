"""Export and validate a trained PM2.5 model as TensorFlow Lite."""

import argparse
from dataclasses import dataclass
from pathlib import Path
import shutil
from uuid import uuid4

import numpy as np

from scripts.evaluate import predict_batches
from src.data_pipeline import load_prepared_split
from src.preprocessing import inverse_targets, load_scalers
from src.utils import (
    FEATURES,
    HORIZON_HOURS,
    LOOKBACK_HOURS,
    dataset_fingerprint,
    read_json,
    sha256,
    write_json,
)

CHECKPOINT_NAME = "best.keras"
SCALERS_NAME = "scalers.json"
SCALERS_HEADER_NAME = "scalers.h"
MODEL_NAME = "model.tflite"
MODEL_INFO_NAME = "model_info.json"
DEFAULT_OUTPUT_DIR = Path(__file__).resolve().parents[1] / "runs" / "exports"
INPUT_SHAPE = (1, LOOKBACK_HOURS, len(FEATURES))
OUTPUT_SHAPE = (1, HORIZON_HOURS)
QUANTIZATION_MODES = ("float32", "int8")
CALIBRATION_SAMPLES = 200
INT8_MIN, INT8_MAX = -128, 127


@dataclass(frozen=True)
class ExportSource:
    checkpoint: Path
    processed_dir: Path
    checkpoint_sha256: str
    dataset_fingerprint: str


def _resolve_checkpoint(path: Path) -> Path:
    path = path.expanduser().resolve()
    if path.is_dir():
        path /= CHECKPOINT_NAME
    if not path.is_file() or path.suffix.lower() != ".keras":
        raise ValueError(f"Expected a .keras checkpoint or run directory: {path}")
    return path


def _resolve_processed_dir(override: Path | None, metadata: dict) -> Path:
    if override is not None:
        path = override
    elif isinstance(metadata.get("processed_dir"), str) and metadata["processed_dir"]:
        path = Path(metadata["processed_dir"])
    else:
        raise ValueError("Pass --processed-dir to locate the original training dataset")

    path = path.expanduser().resolve()
    if not path.is_dir():
        raise ValueError(f"Dataset directory not found: {path}. Pass --processed-dir.")
    return path


def _load_source(checkpoint: Path, processed_dir: Path | None) -> ExportSource:
    checkpoint = _resolve_checkpoint(checkpoint)
    metadata_path = checkpoint.with_name("run_metadata.json")
    metadata = read_json(metadata_path) if metadata_path.is_file() else {}
    processed = _resolve_processed_dir(processed_dir, metadata)
    recorded_checkpoint = metadata.get("checkpoint_sha256")
    recorded_dataset = metadata.get("dataset_fingerprint")
    checkpoint_hash = sha256(checkpoint)
    if recorded_checkpoint is not None and checkpoint_hash != recorded_checkpoint:
        raise ValueError("Checkpoint checksum does not match run_metadata.json")
    fingerprint = dataset_fingerprint(processed)
    if recorded_dataset is not None and fingerprint != recorded_dataset:
        raise ValueError("Dataset fingerprint does not match run_metadata.json")
    return ExportSource(checkpoint, processed, checkpoint_hash, fingerprint)


def _calibration_windows(processed_dir: Path) -> np.ndarray:
    windows = load_prepared_split(processed_dir, "train").X
    count = min(CALIBRATION_SAMPLES, len(windows))
    indices = np.linspace(0, len(windows) - 1, count, dtype=np.intp)
    return windows[indices]


def _inference_model(tf, model, *, unroll: bool):
    """Remove recurrent training state and expand loops for calibration."""
    recurrent_types = (tf.keras.layers.LSTM, tf.keras.layers.GRU)
    needs_clone = any(
        isinstance(layer, recurrent_types)
        and (unroll or layer.dropout or layer.recurrent_dropout)
        for layer in model.layers
    )
    if not needs_clone:
        return model

    def clone_layer(layer):
        config = layer.get_config()
        if isinstance(layer, recurrent_types):
            # Dropout is inactive at inference but can still trace RNG state updates.
            config.update(dropout=0.0, recurrent_dropout=0.0)
            if unroll:
                config["unroll"] = True
        return type(layer).from_config(config)

    export_model = tf.keras.models.clone_model(
        model,
        input_tensors=tf.keras.Input(batch_shape=INPUT_SHAPE, name="history"),
        clone_function=clone_layer,
    )
    export_model.set_weights(model.get_weights())
    return export_model


def _fixed_forecast(tf, model):
    def forecast(history):
        return model(history, training=False)

    traced_forecast = tf.function(
        forecast,
        input_signature=[tf.TensorSpec(INPUT_SHAPE, tf.float32, name="history")],
        autograph=False,
    )
    concrete = traced_forecast.get_concrete_function()
    output = concrete.structured_outputs
    if not tf.is_tensor(output) or output.shape != OUTPUT_SHAPE:
        raise ValueError(f"Expected a single Keras output with shape {OUTPUT_SHAPE}")
    if output.dtype != tf.float32:
        raise ValueError(f"Expected a float32 Keras output, got {output.dtype.name}")
    return concrete


def _configure_converter(tf, converter, calibration: np.ndarray | None) -> None:
    if calibration is None:
        converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS]
        return

    def representative_dataset():
        for window in calibration:
            yield [window[np.newaxis, ...]]

    converter.optimizations = [tf.lite.Optimize.DEFAULT]
    converter.representative_dataset = representative_dataset
    converter.target_spec.supported_ops = [tf.lite.OpsSet.TFLITE_BUILTINS_INT8]
    converter.inference_input_type = tf.int8
    converter.inference_output_type = tf.int8


def _convert_saved_model(tf, directory: str, calibration: np.ndarray | None) -> bytes:
    converter = tf.lite.TFLiteConverter.from_saved_model(directory)
    _configure_converter(tf, converter, calibration)
    flatbuffer = converter.convert()
    if calibration is None:
        return flatbuffer

    try:
        _require_integer_tensors(flatbuffer)
    except ValueError:
        # The MLIR quantizer can retain a float Split inside an unrolled LSTM.
        converter.experimental_new_quantizer = False
        flatbuffer = converter.convert()
        _require_integer_tensors(flatbuffer)
    return flatbuffer


def _convert_model(
    tf, model, saved_model_dir: Path, calibration: np.ndarray | None
) -> bytes:
    from tensorflow.lite.python.convert import ConverterError
    from tensorflow.python.framework.convert_to_constants import (
        convert_variables_to_constants_v2,
    )

    try:
        inference_model = _inference_model(tf, model, unroll=calibration is not None)
        # Freeze Keras weights so recurrent subgraphs can also be calibrated.
        frozen = convert_variables_to_constants_v2(_fixed_forecast(tf, inference_model))
        directory = saved_model_dir.as_posix()
        tf.saved_model.save(
            tf.Module(), directory, signatures={"serving_default": frozen}
        )
        return _convert_saved_model(tf, directory, calibration)
    except (ConverterError, tf.errors.OpError, RuntimeError, ValueError) as exc:
        mode = "float32" if calibration is None else "int8"
        raise RuntimeError(f"TFLite {mode} conversion failed: {exc}") from exc


def _require_integer_tensors(flatbuffer: bytes) -> None:
    from tensorflow.lite.python import schema_py_generated as schema

    model = schema.Model.GetRootAs(flatbuffer, 0)
    float_types = {
        value
        for name, value in vars(schema.TensorType).items()
        if name.startswith("FLOAT") or name == "BFLOAT16"
    }
    for subgraph_index in range(int(model.SubgraphsLength())):
        subgraph = model.Subgraphs(subgraph_index)
        if subgraph is None:
            raise ValueError(f"Missing TFLite subgraph {subgraph_index}")

        tensor_count = int(subgraph.TensorsLength())
        for tensor_index in range(tensor_count):
            tensor = subgraph.Tensors(tensor_index)
            if tensor is None:
                raise ValueError(
                    f"Missing TFLite tensor {tensor_index} in subgraph {subgraph_index}"
                )
            if tensor.Type() in float_types:
                raise ValueError(
                    f"Int8 conversion left float tensors in subgraph {subgraph_index}"
                )


def _model_operators(flatbuffer: bytes) -> tuple[int, list[dict]]:
    from tensorflow.lite.python import schema_py_generated as schema

    model = schema.Model.GetRootAs(flatbuffer, 0)
    names = {
        code: name
        for name, code in vars(schema.BuiltinOperator).items()
        if isinstance(code, int)
    }
    operators = []
    for index in range(int(model.OperatorCodesLength())):
        operator = model.OperatorCodes(index)
        if operator is None:
            raise ValueError(f"Missing TFLite operator code {index}")
        code = int(max(operator.BuiltinCode(), operator.DeprecatedBuiltinCode()))
        name = names.get(code)
        if name is None or code == schema.BuiltinOperator.CUSTOM:
            label = operator.CustomCode() or code
            raise ValueError(f"Unsupported TFLite operator: {label}")
        operators.append({"name": name, "version": int(operator.Version())})
    return int(model.Version()), operators


def _tensor_info(details, shape: tuple[int, ...], mode: str) -> dict:
    if len(details) != 1:
        raise ValueError("Expected a single TFLite input and a single TFLite output")
    tensor = details[0]
    name = tensor["name"]
    dtype = np.dtype(tensor["dtype"])
    if tuple(tensor["shape_signature"]) != shape or tuple(tensor["shape"]) != shape:
        raise ValueError(f"Expected fixed TFLite tensor shape {shape}: {name}")
    if dtype.name != mode:
        raise ValueError(f"Expected {mode} tensor, got {dtype.name}: {name}")

    info = {"name": name, "shape": list(shape), "dtype": dtype.name}
    if mode == "int8":
        scale, zero_point = tensor["quantization"]
        if (
            not np.isfinite(scale)
            or scale <= 0
            or not INT8_MIN <= zero_point <= INT8_MAX
        ):
            raise ValueError(f"Invalid int8 quantization: {name}")
        info.update(scale=float(scale), zero_point=int(zero_point))
    return info


def _predict_tflite(
    interpreter, windows: np.ndarray, inputs: dict, outputs: dict
) -> np.ndarray:
    input_index = interpreter.get_input_details()[0]["index"]
    output_index = interpreter.get_output_details()[0]["index"]
    quantized = inputs["dtype"] == "int8"
    if quantized:
        quantized_inputs = np.rint(windows / inputs["scale"]) + inputs["zero_point"]
        windows = np.clip(quantized_inputs, INT8_MIN, INT8_MAX).astype(np.int8)

    predictions = np.empty((len(windows), HORIZON_HOURS), dtype=np.float32)
    for index, window in enumerate(windows):
        interpreter.reset_all_variables()
        interpreter.set_tensor(input_index, window[np.newaxis, ...])
        interpreter.invoke()
        predictions[index] = interpreter.get_tensor(output_index)[0]

    if quantized:
        predictions = (predictions - outputs["zero_point"]) * outputs["scale"]
    return predictions


def _validation_report(
    model, interpreter, validation, scalers, inputs: dict, outputs: dict
) -> dict:
    keras = inverse_targets(predict_batches(model, validation.X), scalers)
    tflite_predictions = _predict_tflite(interpreter, validation.X, inputs, outputs)
    tflite = inverse_targets(tflite_predictions, scalers)
    actual = inverse_targets(validation.y, scalers)
    differences = np.abs(tflite - keras)
    return {
        "split": "validation",
        "samples": len(actual),
        "units": "ug/m3",
        "per_horizon": [
            {
                "hour": hour + 1,
                "mean_abs_difference": float(differences[:, hour].mean()),
                "max_abs_difference": float(differences[:, hour].max()),
                "keras_mae": float(np.abs(keras[:, hour] - actual[:, hour]).mean()),
                "tflite_mae": float(np.abs(tflite[:, hour] - actual[:, hour]).mean()),
            }
            for hour in range(HORIZON_HOURS)
        ],
    }


def _model_info(tf, model, flatbuffer: bytes, source: ExportSource, mode: str) -> dict:
    schema_version, operators = _model_operators(flatbuffer)
    resolver = tf.lite.experimental.OpResolverType.BUILTIN_WITHOUT_DEFAULT_DELEGATES
    interpreter = tf.lite.Interpreter(
        model_content=flatbuffer,
        experimental_op_resolver_type=resolver,
    )
    interpreter.allocate_tensors()
    inputs = _tensor_info(interpreter.get_input_details(), INPUT_SHAPE, mode)
    outputs = _tensor_info(interpreter.get_output_details(), OUTPUT_SHAPE, mode)
    validation = load_prepared_split(source.processed_dir, "validation")
    scalers = load_scalers(source.processed_dir / SCALERS_NAME)
    report = _validation_report(
        model, interpreter, validation, scalers, inputs, outputs
    )
    return {
        "quantization": mode,
        "model_size_bytes": len(flatbuffer),
        "tflite_schema_version": schema_version,
        "checkpoint_sha256": source.checkpoint_sha256,
        "dataset_fingerprint": source.dataset_fingerprint,
        "feature_order": list(FEATURES),
        "input_step_hours": 1,
        "output_horizon_hours": list(range(1, HORIZON_HOURS + 1)),
        "input": inputs,
        "output": outputs,
        "operators": operators,
        "validation": report,
    }


# Generate scalers.h for firmware from the exported scaler JSON.
def _format_c_float(value: float) -> str:
    return f"{value:.9e}f"


def _render_scalers_header(scalers) -> str:
    feature_order = ", ".join(scalers.features)
    count = len(scalers.features)
    x_mean = ", ".join(_format_c_float(value) for value in scalers.x.mean_)
    x_scale = ", ".join(_format_c_float(value) for value in scalers.x.scale_)
    y_mean = _format_c_float(scalers.y.mean_[0])
    y_scale = _format_c_float(scalers.y.scale_[0])
    return f"""#ifndef SCALERS_H
#define SCALERS_H

/* Generated by scripts/export.py from scalers.json. */
/* Feature order: {feature_order}. */
static const float pm25_input_mean[{count}] = {{ {x_mean} }};
static const float pm25_input_scale[{count}] = {{ {x_scale} }};
static const float pm25_output_mean = {y_mean};
static const float pm25_output_scale = {y_scale};

#endif
"""


def _write_scalers_header(scalers_path: Path) -> None:
    scalers = load_scalers(scalers_path)
    header = scalers_path.with_name(SCALERS_HEADER_NAME)
    header.write_text(_render_scalers_header(scalers), encoding="utf-8")


def _check_output_dir(output: Path) -> None:
    if output.is_symlink() or (
        output.exists() and (not output.is_dir() or any(output.iterdir()))
    ):
        raise FileExistsError(
            f"Export destination already exists and is not an empty directory: {output}"
        )


def _publish_to_empty_directory(artifacts: Path, output: Path) -> None:
    created = []
    try:
        for name in (MODEL_NAME, SCALERS_NAME, SCALERS_HEADER_NAME, MODEL_INFO_NAME):
            target = output / name
            with (artifacts / name).open("rb") as source, target.open("xb") as dest:
                created.append(target)
                shutil.copyfileobj(source, dest)
    except BaseException:
        for target in reversed(created):
            target.unlink(missing_ok=True)
        raise


def _publish_export(
    staging: Path, output: Path, flatbuffer: bytes, info: dict, source: ExportSource
) -> None:
    artifacts = staging / "artifacts"
    artifacts.mkdir()
    (artifacts / MODEL_NAME).write_bytes(flatbuffer)
    scalers_path = artifacts / SCALERS_NAME
    shutil.copyfile(source.processed_dir / SCALERS_NAME, scalers_path)
    _write_scalers_header(scalers_path)
    write_json(artifacts / MODEL_INFO_NAME, info)
    if sha256(source.checkpoint) != source.checkpoint_sha256:
        raise ValueError("Checkpoint changed during export")
    if dataset_fingerprint(source.processed_dir) != source.dataset_fingerprint:
        raise ValueError("Dataset changed during export")
    _check_output_dir(output)
    if output.is_dir():
        _publish_to_empty_directory(artifacts, output)
    else:
        artifacts.rename(output)


def export_checkpoint(
    checkpoint: Path,
    output_dir: Path,
    *,
    quantization: str = "float32",
    processed_dir: Path | None = None,
) -> Path:
    """Publish a validated TFLite model and deployment metadata together."""
    output = output_dir.expanduser().absolute()
    _check_output_dir(output)
    if quantization not in QUANTIZATION_MODES:
        raise ValueError(f"Unsupported quantization: {quantization}")

    source = _load_source(checkpoint, processed_dir)
    calibration = None
    if quantization == "int8":
        calibration = _calibration_windows(source.processed_dir)

    import tensorflow as tf

    output = output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    staging = output.parent / (".export_" + uuid4().hex)
    staging.mkdir()
    try:
        with tf.device("/CPU:0"):
            model = tf.keras.models.load_model(source.checkpoint, compile=False)
            flatbuffer = _convert_model(tf, model, staging / "saved_model", calibration)
            info = _model_info(tf, model, flatbuffer, source, quantization)
        _publish_export(staging, output, flatbuffer, info, source)
    except tf.errors.OpError as exc:
        raise RuntimeError(f"TensorFlow export failed: {exc}") from exc
    finally:
        shutil.rmtree(staging)
    return output


def _build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--checkpoint", type=Path, required=True, help=".keras file or run directory"
    )
    parser.add_argument(
        "--output-dir",
        type=Path,
        default=DEFAULT_OUTPUT_DIR,
        help="New or empty export directory (default: runs/exports)",
    )
    parser.add_argument(
        "--quantization",
        choices=QUANTIZATION_MODES,
        default="float32",
        help="TensorFlow Lite numeric format (default: float32)",
    )
    parser.add_argument(
        "--processed-dir",
        type=Path,
        help="Original prepared dataset (default: run_metadata.json)",
    )
    return parser


def main(argv=None) -> None:
    parser = _build_parser()
    args = parser.parse_args(argv)
    try:
        output = export_checkpoint(
            args.checkpoint,
            args.output_dir,
            quantization=args.quantization,
            processed_dir=args.processed_dir,
        )
    except (ImportError, OSError, RuntimeError, ValueError) as exc:
        parser.exit(1, f"{parser.prog}: {exc}\n")
    print(output)


if __name__ == "__main__":
    main()
