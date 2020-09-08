
#include "polynomial.h"
#include <Eigen/Dense>
#include <vector>

#pragma once

class Lagrange
{
public:
  Lagrange(int dim, int degree);

  Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
  tabulate_basis(const Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic,
                                    Eigen::RowMajor>& pts);

private:
  int _dim;
  int _degree;
  std::vector<Polynomial> poly_set;
};
