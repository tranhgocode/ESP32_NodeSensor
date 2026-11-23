"""Build LSTM using the layers declared in its YAML config."""

import keras

LAYER_TYPES = {
    "LSTM": keras.layers.LSTM,
    "Dense": keras.layers.Dense,
    "Dropout": keras.layers.Dropout,
}


def build_model(config):
    """Pass the model section of configs/lstm.yaml."""
    model = keras.Sequential(name=config["name"])
    model.add(keras.Input(shape=tuple(config["input_shape"]), name="history"))

    for layer_config in config["layers"]:
        params = layer_config.copy()
        layer_type = params.pop("type")

        if layer_type not in LAYER_TYPES:
            raise ValueError(f"Unsupported layer type: {layer_type}")

        model.add(LAYER_TYPES[layer_type](**params))

    return model
