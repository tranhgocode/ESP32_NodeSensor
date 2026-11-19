"""Build a forecasting model from the model section of a parsed YAML config."""

from .cnn_lstm import build_model as build_cnn_lstm
from .conv1d_gru import build_model as build_conv1d_gru
from .lstm import build_model as build_lstm
from .se_lstm import build_model as build_se_lstm


MODEL_BUILDERS = {
    "cnn_lstm": build_cnn_lstm,
    "conv1d_gru": build_conv1d_gru,
    "lstm": build_lstm,
    "se_lstm": build_se_lstm,
}


def build_model(config):
    """Call build_model(yaml_config['model']), training compiles the result."""
    name = config["name"]
    if name not in MODEL_BUILDERS:
        raise ValueError(f"Unknown model: {name}")

    return MODEL_BUILDERS[name](config)
