// Copyright (c) 2020 Chris Richardson & Matthew Scroggs
// FEniCS Project
// SPDX-License-Identifier:    MIT

#include "nedelec.h"
#include "integral-moments.h"
#include "lagrange.h"
#include "polynomial-set.h"
#include "quadrature.h"
#include <Eigen/Dense>
#include <Eigen/SVD>
#include <iostream>
#include <numeric>
#include <vector>

using namespace libtab;

namespace
{

Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
create_nedelec_2d_space(int degree)
{
  // 2D space on triangle
  const int tdim = 2;

  // Vector subset
  const int nv = (degree + 1) * (degree + 2) / 2;

  // PkH subset
  const int ns = degree + 1;
  const int ns0 = (degree + 1) * degree / 2;

  // Tabulate P(k+1) at quadrature points
  auto [Qpts, Qwts] = Quadrature::make_quadrature(tdim, 2 * degree + 2);
  Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
      Pkp1_at_Qpts
      = PolynomialSet::tabulate(Cell::Type::triangle, degree + 1, 0, Qpts)[0];

  const int psize = Pkp1_at_Qpts.cols();

  // Create initial coefficients of Pkp1.
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
      wcoeffs(nv * 2 + ns, psize * 2);
  wcoeffs.setZero();
  wcoeffs.block(0, 0, nv, nv) = Eigen::MatrixXd::Identity(nv, nv);
  wcoeffs.block(nv, psize, nv, nv) = Eigen::MatrixXd::Identity(nv, nv);

  for (int i = 0; i < ns; ++i)
    for (int k = 0; k < psize; ++k)
    {
      auto w0 = Qwts * Pkp1_at_Qpts.col(ns0 + i) * Qpts.col(1)
                * Pkp1_at_Qpts.col(k);
      wcoeffs(2 * nv + i, k) = w0.sum();

      auto w1 = -Qwts * Pkp1_at_Qpts.col(ns0 + i) * Qpts.col(0)
                * Pkp1_at_Qpts.col(k);
      wcoeffs(2 * nv + i, k + psize) = w1.sum();
    }
  return wcoeffs;
}
//-----------------------------------------------------------------------------
Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
create_nedelec_2d_dual(int degree)
{
  // Number of dofs and size of polynomial set P(k+1)
  const int ndofs = 3 * (degree + 1) + degree * (degree + 1);
  const int psize = (degree + 2) * (degree + 3) / 2;

  // Dual space
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
      dualmat(ndofs, psize * 2);
  dualmat.setZero();

  // dof counter
  int quad_deg = 5 * (degree + 1);

  // Integral representation for the boundary (edge) dofs
  Lagrange moment_space_E(Cell::Type::interval, degree);
  dualmat.block(0, 0, 3 * (degree + 1), psize * 2)
      = IntegralMoments::make_tangent_integral_moments(
          moment_space_E, Cell::Type::triangle, 2, degree + 1, quad_deg);

  if (degree > 0)
  {
    // Interior integral moment
    Lagrange moment_space_I(Cell::Type::triangle, degree - 1);
    dualmat.block(3 * (degree + 1), 0, degree * (degree + 1), psize * 2)
        = IntegralMoments::make_integral_moments(
            moment_space_I, Cell::Type::triangle, 2, degree + 1, quad_deg);
  }
  return dualmat;
}
//-----------------------------------------------------------------------------
Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
create_nedelec_3d_space(int degree)
{
  // Reference tetrahedron
  const int tdim = 3;

  // Vector subset
  const int nv = (degree + 1) * (degree + 2) * (degree + 3) / 6;

  // PkH subset
  const int ns = (degree + 1) * (degree + 2) / 2;
  const int ns0 = degree * (degree + 1) * (degree + 2) / 6;

  // Tabulate P(k+1) at quadrature points
  auto [Qpts, Qwts] = Quadrature::make_quadrature(tdim, 2 * degree + 2);
  Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
      Pkp1_at_Qpts = PolynomialSet::tabulate(Cell::Type::tetrahedron,
                                             degree + 1, 0, Qpts)[0];
  const int psize = Pkp1_at_Qpts.cols();

  // Create initial coefficients of Pkp1.
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
      wcoeffs((nv + ns) * tdim, psize * tdim);
  wcoeffs.setZero();
  for (int i = 0; i < tdim; ++i)
    wcoeffs.block(nv * i, psize * i, nv, nv)
        = Eigen::MatrixXd::Identity(nv, nv);

  for (int i = 0; i < ns; ++i)
    for (int k = 0; k < psize; ++k)
      for (int j = 0; j < tdim; ++j)
      {
        const int j1 = (j + 1) % 3;
        const int j2 = (j + 2) % 3;

        auto w = Qwts * Pkp1_at_Qpts.col(ns0 + i) * Qpts.col(j)
                 * Pkp1_at_Qpts.col(k);
        wcoeffs(tdim * nv + i + ns * j1, psize * j2 + k) = -w.sum();
        wcoeffs(tdim * nv + i + ns * j2, psize * j1 + k) = w.sum();
      }

  // Remove dependent components from space with SVD
  Eigen::JacobiSVD<
      Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
      svd(wcoeffs, Eigen::ComputeThinU | Eigen::ComputeThinV);

  int ndofs = 6 * (degree + 1) + 4 * degree * (degree + 1)
              + (degree - 1) * degree * (degree + 1) / 2;
  wcoeffs = svd.matrixV().transpose().topRows(ndofs);

  // Check singular values (should only be ndofs which are significant)
  Eigen::VectorXd s = svd.singularValues();
  for (int i = 0; i < ndofs; ++i)
    if (s[i] < 1e-12)
      throw std::runtime_error("Error in Nedelec3D space");
  for (int i = ndofs; i < s.size(); ++i)
    if (s[i] > 1e-12)
      throw std::runtime_error("Error in Nedelec3D space");

  return wcoeffs;
}
//-----------------------------------------------------------------------------
Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
create_nedelec_3d_dual(int degree)
{
  const int tdim = 3;

  // Size of polynomial set P(k+1)
  const int psize = (degree + 2) * (degree + 3) * (degree + 4) / 6;

  // Work out number of dofs
  const int ndofs = 6 * (degree + 1) + 4 * degree * (degree + 1)
                    + (degree - 1) * degree * (degree + 1) / 2;
  Eigen::Matrix<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
      dualmat(ndofs, psize * tdim);
  dualmat.setZero();

  // Create quadrature scheme on the edge
  int quad_deg = 5 * (degree + 1);

  // Integral representation for the boundary (edge) dofs
  Lagrange moment_space_E(Cell::Type::interval, degree);
  dualmat.block(0, 0, 6 * (degree + 1), psize * 3)
      = IntegralMoments::make_tangent_integral_moments(
          moment_space_E, Cell::Type::tetrahedron, 3, degree + 1, quad_deg);

  if (degree > 0)
  {
    // Integral moments on faces
    Lagrange moment_space_F(Cell::Type::triangle, degree - 1);
    dualmat.block(6 * (degree + 1), 0, 4 * degree * (degree + 1), psize * 3)
        = IntegralMoments::make_integral_moments(
            moment_space_F, Cell::Type::tetrahedron, 3, degree + 1, quad_deg);
  }

  if (degree > 1)
  {
    // Interior integral moment
    Lagrange moment_space_I(Cell::Type::tetrahedron, degree - 2);
    dualmat.block(6 * (degree + 1) + 4 * degree * (degree + 1), 0,
                  (degree - 1) * degree * (degree + 1) / 2, psize * 3)
        = IntegralMoments::make_integral_moments(
            moment_space_I, Cell::Type::tetrahedron, 3, degree + 1, quad_deg);
  }

  return dualmat;
}

} // namespace

//-----------------------------------------------------------------------------
Nedelec::Nedelec(Cell::Type celltype, int k) : FiniteElement(celltype, k - 1)
{

  Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> wcoeffs;
  Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor> dualmat;

  if (celltype == Cell::Type::triangle)
  {
    wcoeffs = create_nedelec_2d_space(_degree);
    dualmat = create_nedelec_2d_dual(_degree);
  }
  else if (celltype == Cell::Type::tetrahedron)
  {
    wcoeffs = create_nedelec_3d_space(_degree);
    dualmat = create_nedelec_3d_dual(_degree);
  }
  else
    throw std::runtime_error("Invalid celltype in Nedelec");

  apply_dualmat_to_basis(wcoeffs, dualmat);
}
//-----------------------------------------------------------------------------
std::vector<
    Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
Nedelec::tabulate(int nderiv,
                  const Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic,
                                     Eigen::RowMajor>& pts) const
{
  const int tdim = Cell::topological_dimension(_cell_type);
  if (pts.cols() != tdim)
    throw std::runtime_error(
        "Point dimension does not match element dimension");

  std::vector<
      Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
      Pkp1_at_pts
      = PolynomialSet::tabulate(_cell_type, _degree + 1, nderiv, pts);
  const int psize = Pkp1_at_pts[0].cols();
  const int ndofs = _coeffs.rows();

  std::vector<
      Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>>
      dresult(Pkp1_at_pts.size());

  for (std::size_t p = 0; p < dresult.size(); ++p)
  {
    dresult[p].resize(pts.rows(), ndofs * tdim);
    for (int j = 0; j < tdim; ++j)
      dresult[p].block(0, ndofs * j, pts.rows(), ndofs)
          = Pkp1_at_pts[p].matrix()
            * _coeffs.block(0, psize * j, _coeffs.rows(), psize).transpose();
  }

  return dresult;
}
//-----------------------------------------------------------------------------
