/* The image decoder's code. Chiplet's Octo compiler reads pictures a source
 * file includes; upstream gets these functions from its window library,
 * which this core does not have. The decoder is the header Chiplet carries
 * (public domain, or MIT). */
#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_THREAD_LOCALS
#include <chiplet/stb_image.h>
