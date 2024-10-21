#include <getopt.h>
#include <stdio.h>
#include "VTOP.h"
#include "VTOP___024root.h"

#include <fstream>
#include <string>

std::ofstream csv_file;
std::ofstream param_file_stream;
int line_count = 0;
int data_count = 0;

// #ifdef CONFIG_TRACE
#define TRACE
// #endif
#ifdef TRACE
#ifndef CONFIG_TRACE_MAX
#define CONFIG_TRACE_MAX 1000000
#endif
#include "verilated_vcd_c.h"
VerilatedVcdC* tfp = NULL;
#endif
VerilatedContext* contextp = NULL;
VTOP* top = NULL;
int trace_time = 0;

int image_width = 640, image_height = 480;
char* image_file;
char* param_file;
char* output_dir;
char* output_name;
std::string csv_file_path;
std::string param_file_path;

bool prev_post_frame_vsync = false;
bool prev_post_frame_href = false;
bool prev_post_frame_clken = false;

void monitor_output() {
  // monitoring output
  // posedge clken
  if (!prev_post_frame_clken && top->post_frame_clken) {
    if (top->post_frame_href && top->post_frame_vsync) {
      csv_file << static_cast<int>(top->post_img_Y) << ","
               << static_cast<int>(top->post_img_Cb) << ","
               << static_cast<int>(top->post_img_Cr) << "\n";
      printf("Y: %d, Cb: %d, Cr: %d, X: %d, y: %d\n",
             static_cast<int>(top->post_img_Y),
             static_cast<int>(top->post_img_Cb),
             static_cast<int>(top->post_img_Cr), data_count, line_count);
      data_count++;
    }
  }

  // negedge href
  if (prev_post_frame_href && !top->post_frame_href) {
    line_count++;
    data_count = 0;
  }

  // negedge vsync
  if (prev_post_frame_vsync && !top->post_frame_vsync && line_count > 0) {
    param_file_stream << "Height: " << line_count << "\n";
    param_file_stream << "Width: " << image_width << "\n";
    printf("Height: %d, Width: %d\n", line_count, image_width);
    csv_file.close();
    param_file_stream.close();
  }

  prev_post_frame_vsync = top->post_frame_vsync;
  prev_post_frame_href = top->post_frame_href;
  prev_post_frame_clken = top->post_frame_clken;
}

void step() {
  top->clk = 0;
  top->eval();
  monitor_output();
#ifdef TRACE
  if (trace_time++ < CONFIG_TRACE_MAX) {
    tfp->dump(contextp->time());
    contextp->timeInc(1);
  }
#endif
  top->clk = 1;
  top->eval();
  monitor_output();
#ifdef TRACE
  if (trace_time++ < CONFIG_TRACE_MAX) {
    tfp->dump(contextp->time());
    contextp->timeInc(1);
  }
#endif
}

void step_no_inst() {
  top->clk = 0;
  top->eval();
#ifdef TRACE
  if (trace_time++ < CONFIG_TRACE_MAX) {
    tfp->dump(contextp->time());
    contextp->timeInc(1);
  }
#endif
  top->clk = 1;
  top->eval();
#ifdef TRACE
  if (trace_time++ < CONFIG_TRACE_MAX) {
    tfp->dump(contextp->time());
    contextp->timeInc(1);
  }
#endif
}

void stepn(int n) {
  while (n--) {
    step();
  }
}

void reset(int n) {
  top->rst_n = 0;
  while (n--) {
    step_no_inst();
  }
  top->rst_n = 1;
}

void load_image_data(const char* csv_file_path) {
  FILE* file = fopen(csv_file_path, "r");
  if (!file) {
    perror("Failed to open CSV file");
    return;
  }

  int r, g, b;
  int hcnt = 0, vcnt = 0;
  top->per_frame_vsync = 1;
  while (fscanf(file, "%d,%d,%d", &r, &g, &b) == 3) {
    top->per_img_red = r;
    top->per_img_green = g;
    top->per_img_blue = b;
    top->per_frame_href = 1;
    top->per_frame_clken = 1;
    step();
    top->per_frame_clken = 0;
    step();
    hcnt++;
    if (hcnt == image_width) {
      hcnt = 0;
      vcnt++;
      top->per_frame_href = 0;
      stepn(5);
      printf("End of line[%d]\n", vcnt);
      if (vcnt == image_height) {
        top->per_frame_vsync = 0;
        printf("End of image\n");
        stepn(1000);
        break;
      }
    }
  }
  fclose(file);
}

void load_param_data(const char* param_file_path) {
  FILE* file = fopen(param_file_path, "r");
  if (!file) {
    perror("Failed to open parameter file");
    return;
  }
  char line[256];
  while (fgets(line, sizeof(line), file)) {
    if (sscanf(line, "Height: %d", &image_height) == 1) {
      printf("Image height: %d\n", image_height);
      continue;
    }
    if (sscanf(line, "Width: %d", &image_width) == 1) {
      printf("Image width: %d\n", image_width);
      continue;
    }
  }
  fclose(file);
}

static int parse_args(int argc, char* argv[]) {
  const struct option table[] = {
      {"log", required_argument, NULL, 'l'},
      {"csv", required_argument, NULL, 'c'},
      {"param", required_argument, NULL, 'p'},
      {"output", required_argument, NULL, 'o'},
      {"target", required_argument, NULL, 't'},
      {"help", no_argument, NULL, 'h'},
      {0, 0, NULL, 0},
  };
  int o;
  while ((o = getopt_long(argc, argv, "-hl:c:p:o:t:", table, NULL)) != -1) {
    switch (o) {
      case 'l':
        // log_file = optarg;
        break;
      case 'd':
        break;
      case 'c':
        image_file = optarg;
        break;
      case 'p':
        param_file = optarg;
        break;
      case 'o':
        output_dir = optarg;
        break;
      case 't':
        output_name = optarg;
        break;
      default:
        printf("Usage: %s [OPTION...] IMAGE [args]\n\n", argv[0]);
        printf("\t-b,--batch              run with batch mode\n");
        printf("\t-l,--log=FILE           output log to FILE\n");
        printf("\t-e,--elf=FILE           load elf FILE\n");
        printf(
            "\t-d,--diff=REF_SO        run DiffTest with reference REF_SO\n");
        printf("\t-p,--port=PORT          run DiffTest with port PORT\n");
        printf("\t-o,--output=DIR         specify output directory\n");
        printf("\t-t,--target=NAME         specify output name\n");
        printf("\n");
        exit(0);
    }
  }
  return 0;
}

int main(int argc, char* argv[]) {
  contextp = new VerilatedContext;
  contextp->commandArgs(argc, argv);
  top = new VTOP;
  parse_args(argc, argv);
#ifdef TRACE
  tfp = new VerilatedVcdC;
  contextp->traceEverOn(true);
  top->trace(tfp, 0);
  tfp->open("wave.vcd");
#endif
  reset(10);
  if (image_file == NULL) {
    printf("Please specify the image file using -c option\n");
#ifdef TRACE
    tfp->close();
    delete tfp;
#endif
    delete top;
    delete contextp;
    return 0;
  }
  if (param_file == NULL) {
    printf("Please specify the param file using -p option\n");
  }
  if (output_dir == NULL) {
    printf("Please specify the output directory using -o option\n");
    return 0;
  }

  // Create output files
  std::string csv_file_path =
      std::string(output_dir) + "/" + std::string(output_name) + ".csv";
  std::string param_file_path =
      std::string(output_dir) + "/" + std::string(output_name) + ".txt";
  csv_file.open(csv_file_path);
  param_file_stream.open(param_file_path);

  // Load image data from CSV file

  load_param_data(param_file);
  load_image_data(image_file);

  // Run the simulation for a specified number of cycles
  for (int i = 0; i < 100; ++i) {
    step();
  }
#ifdef TRACE
  tfp->close();
  delete tfp;
#endif
  delete top;
  delete contextp;
  // printf("argc=%d, argv[0]=%s\n", argc, argv[0]);
  return 0;
}