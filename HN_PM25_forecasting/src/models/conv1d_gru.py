"""Build Conv1D-GRU using the layers declared in its YAML config."""

from tensorflow import keras

LAYER_TYPES = {
    "Conv1D": keras.layers.Conv1D,
    "GRU": keras.layers.GRU,
    "Dense": keras.layers.Dense,
    "Dropout": keras.layers.Dropout,
}


def build_model(config):
    """Pass the model section of configs/conv1d_gru.yaml."""
    model = keras.Sequential(name=config["name"])
    model.add(keras.Input(shape=tuple(config["input_shape"]), name="history"))

    for layer_config in config["layers"]:
        params = layer_config.copy()
        layer_type = params.pop("type")

        if layer_type not in LAYER_TYPES:
            raise ValueError(f"Unsupported layer type: {layer_type}")

        model.add(LAYER_TYPES[layer_type](**params))

    return model
