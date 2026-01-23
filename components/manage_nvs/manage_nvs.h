#ifndef MANAGE_NVS_H
#define MANAGE_NVS_H

void save_token_to_nvs(const char *token);
bool load_token_from_nvs();
const char* get_token(void);

#endif 