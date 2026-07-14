// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//#                                                                        #
//#  Generates 2D CAD drawings (plan / elevation) with dimension lines     #
//#  via the self-contained DxfWriter.                                     #
//##########################################################################

#pragma once

#include "BuildingDims.h"
#include "Openings.h"
#include "Planes.h"

#include <QString>
#include <vector>

namespace Drawings
{
	//! Writes a top-down PLAN drawing: footprint + wall segments + overall
	//! length/width dimension lines. \return empty string on success.
	QString writePlan(const BuildingDims::Result& dims,
	                   const Planes::Model&        model,
	                   const QString&              path);

	//! Writes ELEVATION drawings (one framed rectangle per wall, laid out
	//! left-to-right) with width and height dimension lines.
	//! \return empty string on success.
	QString writeElevations(const Planes::Model&                model,
	                         const QString&                      unit,
	                         const QString&                      path,
	                         const std::vector<Openings::Opening>& openings = {});
}
