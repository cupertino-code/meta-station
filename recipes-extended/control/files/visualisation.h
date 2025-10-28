#ifndef _VISUALISATION_H_INCLUDED
#define _VISUALISATION_H_INCLUDED

#define RGB2COLOR(r, g, b) ((b & 0x1F) | ((g & 0x3f) << 5) | ((r & 0x1f) << 11))

#define RADIUS      50
#define MARK_LEN    10
#define X_OFFSET    20.0
#define Y_OFFSET    100.0
#define AREA_WIDTH  152
#define AREA_HEIGHT 500

#ifndef M_PI
#define M_PI        3.14159265358979323846
#endif

int visualisation_init(void);
void visualisation_stop(void);

#endif // _VISUALISATION_H_INCLUDED
