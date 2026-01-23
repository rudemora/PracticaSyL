idf_component_register(SRCS "mqtt_tb.c" "../../proto/telemetry.pb.c" "../../proto/pb_encode.c" "../../proto/pb_common.c"
                    REQUIRES mqtt json manage_nvs
                    INCLUDE_DIRS "." "../../proto"
                    EMBED_TXTFILES ../../certs/server.pem
                    )