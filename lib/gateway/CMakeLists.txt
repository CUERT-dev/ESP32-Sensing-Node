idf_component_register(
    SRCS "src/gateway.c"
    INCLUDE_DIRS "include"
    REQUIRES gp_protocol gp_common
)