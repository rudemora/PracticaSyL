idf_component_register(SRCS "coap_tb.c" "../../proto/telemetry.pb.c" "../../proto/pb_encode.c" "../../proto/pb_common.c"
                    REQUIRES mqtt json manage_nvs coap
                    INCLUDE_DIRS "." "../../proto"
                    #EMBED_TXTFILES ../../certs/server.pem
                    )