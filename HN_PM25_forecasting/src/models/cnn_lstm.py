"""Build CNN-LSTM using the layers declared in its YAML config."""

import keras

LAYER_TYPES = {
    "Conv1D": keras.layers.Conv1D,
    "MaxPooling1D": keras.layers.MaxPooling1D,
    "BatchNormalization": keras.layers.BatchNormalization,
    "LSTM": keras.layers.LSTM,
    "Dense": keras.layers.Dense,
    "Dropout": keras.layers.Dropout,
}


def build_model(config):
    """Pass the model section of configs/cnn_lstm.yaml"""
    # tạo model tuần tự
    model = keras.Sequential(name=config["name"])
    model.add(keras.Input(shape=tuple(config["input_shape"]), name="history"))

    # chạy các layer trong config
    for layer_config in config["layers"]:
        params = layer_config.copy()
        layer_type = params.pop("type")

        # case layer_tpye chưa được định nghĩa ở LATER_TYPES
        if layer_type not in LAYER_TYPES:
            raise ValueError(f"Unsupported layer type: {layer_type}")

        # chuyển đổi cấu hình của yaml sang layer của keras
        if "kernel_regularizer" in params:
            params["kernel_regularizer"] = keras.regularizers.deserialize(
                params["kernel_regularizer"]
            )

        model.add(LAYER_TYPES[layer_type](**params))

    return model
