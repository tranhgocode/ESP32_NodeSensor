    .section .rodata.pm25_model,"a",@progbits
    .balign 16
    .global pm25_model_start
pm25_model_start:
    .incbin PM25_MODEL_PATH
