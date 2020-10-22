// Copyright (c) 2020 Chris Richardson
// FEniCS Project
// SPDX-License-Identifier:    MIT

#include "cell.h"
#include "finite-element.h"

#pragma once

namespace libtab
{
class RaviartThomas : public FiniteElement
{
  /// Raviart-Thomas element of order k
public:
  /// Constructor
  /// @param celltype
  /// @param k degree
  RaviartThomas(Cell::Type celltype, int k);
};
} // namespace libtab
