"""Build Conv1D-GRU with residual depthwise blocks declared in YAML."""

from tensorflow import keras

LAYER_TYPES = {
    "Conv1D": keras.layers.Conv1D,
    "GRU": keras.layers.GRU,
    "Dense": keras.layers.Dense,
    "Dropout": keras.layers.Dropout,
}


def _residual_depthwise_block(values, filters, dilation_rate, dropout, name):
    """Project the input channels, then add the depthwise branch."""
    projection = keras.layers.Conv1D(
        filters, kernel_size=1, padding="causal", name=f"{name}_projection"
    )(values)
    projection = keras.layers.ReLU(name=f"{name}_projection_relu")(projection)
    projection = keras.layers.LayerNormalization(name=f"{name}_projection_norm")(
        projection
    )

    # Preserve the time axis so the branch can be added to the projection.
    branch = keras.layers.DepthwiseConv1D(
        kernel_size=3,
        dilation_rate=dilation_rate,
        padding="same",
        name=f"{name}_depthwise",
    )(projection)
    branch = keras.layers.ReLU(name=f"{name}_depthwise_relu")(branch)
    branch = keras.layers.LayerNormalization(name=f"{name}_depthwise_norm")(branch)
    branch = keras.layers.Dropout(dropout, name=f"{name}_dropout")(branch)
    return keras.layers.Add(name=f"{name}_add")([branch, projection])


def build_model(config):
    """Pass the model section of configs/conv1d_gru.yaml."""
    inputs = keras.Input(shape=tuple(config["input_shape"]), name="history")
    values = inputs

    for layer_config in config["layers"]:
        params = layer_config.copy()
        layer_type = params.pop("type")

        if layer_type == "ResidualDepthwiseConv1D":
            values = _residual_depthwise_block(values, **params)
        elif layer_type in LAYER_TYPES:
            values = LAYER_TYPES[layer_type](**params)(values)
        else:
            raise ValueError(f"Unsupported layer type: {layer_type}")

    return keras.Model(inputs, values, name=config["name"])
