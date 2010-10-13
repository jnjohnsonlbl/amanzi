#include "cell_geometry.hpp"
#include <math.h>

namespace cell_geometry {

  double dot_product(double a[], double b[], int n)
  {
    double dp = 0.0;
    for (int i = 0; i < n; ++i)
      dp += a[i]*b[i];
    return dp;
  }


  double vector_length(double x[], int n)
  {
    double a, b, c, t;
    switch (n) {
      case 2:  // two-vector
        a = fabs(x[0]);
        b = fabs(x[1]);
        // Swap largest value to A.
        if (b > a) {
          t = a;
          a = b;
          b = t;
        }
        return a * sqrt(1.0 + (b/a)*(b/a));

      case 3:  // three-vector
        a = fabs(x[0]);
        b = fabs(x[1]);
        c = fabs(x[2]);
        // Swap largest value to A.
        if (b > a) {
          if (c > b) {
            t = a;
            a = c;
            c = t;
          } else {
            t = a;
            a = b;
            b = t;
          }
        } else if (c > a) {
          t = a;
          a = c;
          c = t;
        }
        return a * sqrt(1.0 + (b/a)*(b/a) + (c/a)*(c/a));

      default:  // Do the na�ve thing for anything else.
        a = 0.0;
        for (int i = 0; i < n; ++i)
          a += x[i]*x[i];
        return sqrt(a);
    }
  }


  void cross_product(double result[], double a[], double b[])
  {
    result[0] = a[1]*b[2] - a[2]*b[1];
    result[1] = a[2]*b[0] - a[0]*b[2];
    result[2] = a[0]*b[1] - a[1]*b[0];
  };


  double triple_product(double a[], double b[], double c[])
  {
    return a[0]*(b[1]*c[2] - b[2]*c[1]) +
           a[1]*(b[2]*c[0] - b[0]*c[2]) + 
           a[2]*(b[0]*c[1] - b[1]*c[0]);
  };


  void quad_face_normal(double result[], double x1[], double x2[], double x3[], double x4[])
  {
    double v1[3], v2[3];
    for (int i = 0; i < 3; ++i) {
      v1[i] = x3[i] - x1[i];
      v2[i] = x4[i] - x2[i];
    }
    cross_product(result, v1, v2);
    for (int i = 0; i < 3; ++i)
      result[i] = 0.5 * result[i];
  }


  double quad_face_area(double x1[], double x2[], double x3[], double x4[])
  {
    double a[3];
    quad_face_normal(a, x1, x2, x3, x4);
    return vector_length(a,3);
  }

  
  double tet_volume(double x1[], double x2[], double x3[], double x4[])
  {
    double v1[3], v2[3], v3[3];

    for (int i = 0; i < 3; ++i) {
      v1[i] = x2[i] - x1[i];
      v2[i] = x3[i] - x1[i];
      v3[i] = x4[i] - x1[i];
    }
    return triple_product(v1, v2, v3) / 6.0;
  }

}
