#include <stdio.h>

#include <zed/Camera.hpp>

#include "GL/freeglut.h"
 
#include "cuda.h"
#include "cuda_runtime.h"
#include "cuda_gl_interop.h"

extern void stereo_init(int, int);
extern void stereo_run(unsigned char *, unsigned char *, unsigned char *);

using namespace sl::zed;

unsigned char *d_display;
GLuint tex_display;
cudaGraphicsResource* pcu_display;
Camera* zed;
int w, h, screenshot;

#define ok(expr) if (expr != 0) { printf("ERROR on line %d\n", __LINE__); exit(-1); }

void save_image(const char *fname, unsigned char *data, int width, int height)
{
    unsigned char *buffer = (unsigned char *)malloc(width * height * 4);
    cudaMemcpy(buffer, data, width * height * 4, cudaMemcpyDeviceToHost);
    FILE *f = fopen(fname, "w");
    fprintf(f, "P3 %d %d 255\n", width, height);
    for (int i = 0; i < width * height; i++) {
        int b = data[i * 4 + 0];
        int g = data[i * 4 + 1];
        int r = data[i * 4 + 2];
        fprintf(f, "%u %u %u\n", r, g, b);
    }
    fclose(f);
    free(buffer);
}

unsigned char *load_image(const char *fname)
{
    int width, height;

    FILE *f = fopen(fname, "r");
    int n = fscanf(f, "P3 %d %d 255\n", &width, &height);
    printf("image size: %d x %d\n", width, height);

    unsigned char *host_buf = (unsigned char *)malloc(width * height * 4);
    for (int i = 0; i < height * width; i++) {
        int r, g, b;

        n = fscanf(f, "%d %d %d\n", &r, &g, &b);
        host_buf[i * 4 + 0] = b;
        host_buf[i * 4 + 1] = g;
        host_buf[i * 4 + 2] = r;
        host_buf[i * 4 + 3] = 0;
    }

    unsigned char *device_buf;
    cudaMalloc((void **)&device_buf, width * height * 4);
    cudaMemcpy(device_buf, host_buf, width * height * 4, cudaMemcpyHostToDevice);

    free(host_buf);
    fclose(f);

    return device_buf;
}


void keyboard(unsigned char key, int x, int y)
{
	switch(key) {
	case 's':
		screenshot = true;
		break;
	case 'q':
		glutDestroyWindow(1);
		break;
	}
}

void draw()
{
	if (zed->grab(SENSING_MODE::RAW, false, false) == 0) {
		Mat left = zed->retrieveImage_gpu(SIDE::LEFT);
		Mat right = zed->retrieveImage_gpu(SIDE::RIGHT);

		if (screenshot) {
			printf("screenshot\n");
			save_image("tmp/left.ppm", left.data, w, h);
			save_image("tmp/right.ppm", right.data, w, h);
			screenshot = 0;
		}

		stereo_run(left.data, right.data, d_display);

		cudaArray_t ArrIm;
		cudaGraphicsMapResources(1, &pcu_display, 0);
		cudaGraphicsSubResourceGetMappedArray(&ArrIm, pcu_display, 0, 0);
		cudaMemcpy2DToArray(ArrIm, 0, 0, d_display, 4 * w, 4 * w, 2 * h, cudaMemcpyDeviceToDevice);
		cudaGraphicsUnmapResources(1, &pcu_display, 0);

		glDrawBuffer(GL_BACK);
		glBindTexture(GL_TEXTURE_2D, tex_display);
		glBegin(GL_QUADS);
		glTexCoord2f(0.0,1.0);
		glVertex2f(-1.0,-1.0);
		glTexCoord2f(1.0,1.0);
		glVertex2f(1.0,-1.0);
		glTexCoord2f(1.0,0.0);
		glVertex2f(1.0,1.0);
		glTexCoord2f(0.0,0.0);
		glVertex2f(-1.0,1.0);
		glEnd();

		glutSwapBuffers();
	}
	glutPostRedisplay();
}


int main(int argc, char **argv) 
{
	if (argc == 1) {
		glutInit(&argc, argv);
		glutInitDisplayMode(GLUT_DOUBLE | GLUT_RGBA | GLUT_DEPTH);
		glutInitWindowPosition(50, 25);
		glutInitWindowSize(640, 720);
		glutCreateWindow("mccnn");
		
		zed = new Camera(sl::zed::HD720, 15.0);
		ok(zed->init(MODE::NONE, 0, true, false));

		w = zed->getImageSize().width;
		h = zed->getImageSize().height;

		glEnable(GL_TEXTURE_2D);	
		glGenTextures(1, &tex_display);
		glBindTexture(GL_TEXTURE_2D, tex_display);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexImage2D(GL_TEXTURE_2D, 0, 4, w, 2 * h, 0, GL_RGBA, GL_UNSIGNED_BYTE, NULL);
		glBindTexture(GL_TEXTURE_2D, 0);
		ok(cudaGraphicsGLRegisterImage(&pcu_display, tex_display, GL_TEXTURE_2D, cudaGraphicsMapFlagsNone));

		stereo_init(w, h);
		cudaMalloc(&d_display, w * h * 4 * 2);

		screenshot = 0;

		glutKeyboardFunc(keyboard);
		glutDisplayFunc(draw);
		glutMainLoop();
	} else {
		printf("stereo test\n");

		w = 1280;
		h = 720;

		unsigned char *d_left = load_image("tmp/left.ppm");
		unsigned char *d_right = load_image("tmp/right.ppm");

		stereo_init(w, h);
		cudaMalloc(&d_display, w * h * 4);
		stereo_run(d_left, d_right, d_display);

	}
	
	return 0;
}
