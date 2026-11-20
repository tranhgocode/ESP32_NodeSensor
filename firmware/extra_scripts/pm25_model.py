Import("env")

# Khai báo phụ thuộc vì SCons không theo dõi tệp nhúng qua .incbin.
env.Depends(
    "$BUILD_DIR/src/forecast/model/pm25_model.S.o",
    "$PROJECT_SRC_DIR/forecast/model/model.tflite",
)
