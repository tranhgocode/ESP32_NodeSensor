"""Build SE-LSTM from YAML, with a channel attention block between layers."""

import keras

LAYER_TYPES = {
    "LSTM": keras.layers.LSTM,
    "Dense": keras.layers.Dense,
    "Dropout": keras.layers.Dropout,
}


def squeeze_excite_1d(values, reduction_ratio=8, name="se"):
    """Learn a weight per hidden channel and apply it to every time step"""
    if type(reduction_ratio) is not int or reduction_ratio <= 0:
        raise ValueError("reduction_ratio must be a positive integer")
    if len(values.shape) != 3 or values.shape[-1] is None:
        raise ValueError("SE requires (batch, time, known_channels)")
    channels = int(values.shape[-1])

    # Squeeze: lấy avg theo thời gian -> (batch, time, channels) -> (batch, channels)
    weights = keras.layers.GlobalAveragePooling1D(name=f"{name}_squeeze")(values)
    weights = keras.layers.Dense(
        max(1, channels // reduction_ratio), 
        activation="relu", 
        name=f"{name}_reduce"
    )(weights)      #  Giảm số chiều của vector tóm tắt, từ 64 -> 8
     
    weights = keras.layers.Dense(
        channels, 
        activation="sigmoid", 
        name=f"{name}_expand"
    )(weights)      # Đưa vector về số kênh ban đầu -> sigmoid để tạo trọng số 
    
    # Reshape: (batch, channels) -> (batch, 1, channels)
    weights = keras.layers.Reshape((1, channels), name=f"{name}_reshape")(weights)
    return keras.layers.Multiply(name=f"{name}_scale")([values, weights])


def build_model(config):
    """Pass the model section of configs/se_lstm.yaml."""
    inputs = keras.Input(shape=tuple(config["input_shape"]), name="history")
    values = inputs

    for layer_config in config["layers"]:
        params = layer_config.copy()
        layer_type = params.pop("type")

        # SqueezeExcite1D là lớp tự custom
        if layer_type == "SqueezeExcite1D":
            values = squeeze_excite_1d(values, **params)
        else:
            if layer_type not in LAYER_TYPES:
                raise ValueError(f"Unsupported layer type: {layer_type}")

            values = LAYER_TYPES[layer_type](**params)(values)

    return keras.Model(inputs, values, name=config["name"])
