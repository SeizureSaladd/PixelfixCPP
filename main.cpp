#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image.h"
#include "stb_image_write.h"

#include <vector>
#include <iostream>
#include <cmath>
#include <thread>
#include <mutex>

struct Pixel { int x, y; unsigned char r, g, b; };

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Usage: pixelfix <file.png> ..." << std::endl;
        return 0;
    }

    for (int f = 1; f < argc; f++) {
        int w, h, channels;
        unsigned char* img =
            stbi_load(argv[f], &w, &h, &channels, 4);
        if (!img) {
            std::cerr << "Failed to load " << argv[f] << std::endl;
            continue;
        }

        std::vector<Pixel> edges;
        edges.reserve(w * h / 10);

        auto idx = [&](int x, int y)->int {
            return (y * w + x) * 4;
            };

        auto isOpaque = [&](int x, int y) {
            if (x < 0 || y < 0 || x >= w || y >= h) return false;
            return img[idx(x, y) + 3] != 0;
            };

        for (int y = 0; y < h; y++) {
            for (int x = 0; x < w; x++) {
                if (img[idx(x, y) + 3] == 0) continue;

                bool neighborTransparent = false;
                static int NB[8][2] = {
                    {-1,-1}, {0,-1}, {1,-1},
                    {1,0}, {1,1}, {0,1}, {-1,1}, {-1,0}
                };

                for (auto& n : NB) {
                    if (!isOpaque(x + n[0], y + n[1])) {
                        neighborTransparent = true;
                        break;
                    }
                }

                if (neighborTransparent) {
                    edges.push_back({ x, y,
                        img[idx(x,y) + 0],
                        img[idx(x,y) + 1],
                        img[idx(x,y) + 2]
                        });
                }
            }
        }

        if (edges.empty()) {
            std::cout << "No transparent pixels to fix in " << argv[f] << std::endl;
            stbi_image_free(img);
            continue;
        }

        const int GRID = 32; // try 16 or 64 for tuning
        int gw = (w + GRID - 1) / GRID;
        int gh = (h + GRID - 1) / GRID;

        std::vector<std::vector<int>> grid(gw * gh);

        for (int i = 0; i < edges.size(); i++) {
            int gx = edges[i].x / GRID;
            int gy = edges[i].y / GRID;
            grid[gy * gw + gx].push_back(i);
        }

        auto nearest = [&](int px, int py) {
            int gx = px / GRID, gy = py / GRID;
            float bestD = 1e9;
            Pixel bestP = { 0,0,0,0,0 };

            for (int oy = -1; oy <= 1; oy++) {
                for (int ox = -1; ox <= 1; ox++) {
                    int nx = gx + ox, ny = gy + oy;
                    if (nx < 0 || ny < 0 || nx >= gw || ny >= gh) continue;

                    for (int idx : grid[ny * gw + nx]) {
                        Pixel& p = edges[idx];
                        float dx = p.x - px;
                        float dy = p.y - py;
                        float d2 = dx * dx + dy * dy;
                        if (d2 < bestD) {
                            bestD = d2;
                            bestP = p;
                        }
                    }
                }
            }
            return bestP;
            };

        int numThreads = std::thread::hardware_concurrency();
        std::vector<std::thread> threads;
        int ystep = (h + numThreads - 1) / numThreads;

        for (int t = 0; t < numThreads; t++) {
            int ystart = t * ystep;
            int yend = std::min(h, (t + 1) * ystep);

            threads.emplace_back([&](int ys, int ye) {
                for (int y = ys; y < ye; y++) {
                    for (int x = 0; x < w; x++) {
                        int i = idx(x, y);
                        if (img[i + 3] == 0) {
                            Pixel p = nearest(x, y);
                            img[i + 0] = p.r;
                            img[i + 1] = p.g;
                            img[i + 2] = p.b;
                            // keep alpha 0
                        }
                    }
                }
                }, ystart, yend);
        }
        for (auto& th : threads) th.join();

        stbi_write_png(argv[f], w, h, 4, img, w * 4);
        std::cout << "Written " << argv[f] << std::endl;

        stbi_image_free(img);
    }

	std::cout << "Press enter to exit..." << std::endl;
	std::cin.get();

    return 0;
}
