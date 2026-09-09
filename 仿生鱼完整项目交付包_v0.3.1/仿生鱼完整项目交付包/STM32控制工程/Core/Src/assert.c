#include "main.h"

#ifdef USE_FULL_ASSERT
/* Assertions use the same best-effort safe shutdown as other HAL failures. */
void assert_failed(uint8_t *file, uint32_t line)
{
    (void)file;
    (void)line;
    Error_Handler();
}
#endif
