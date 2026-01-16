/* FunPay Vertex core result codes. */

#ifndef FPV_RESULT_H
#define FPV_RESULT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef enum fpv_result {
  FPV_OK = 0,
  FPV_ERR_INVALID_ARGUMENT = 1,
  FPV_ERR_OUT_OF_MEMORY = 2,
  FPV_ERR_INVALID_STATE = 3,
  FPV_ERR_INTERNAL = 4,
  FPV_ERR_NOT_FOUND = 5,
  FPV_ERR_IO = 6,
  FPV_ERR_PARSE = 7,
  FPV_ERR_UNSUPPORTED = 8
} fpv_result_t;

#ifdef __cplusplus
}
#endif

#endif
