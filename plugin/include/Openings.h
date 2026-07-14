// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//#                                                                        #
//#  L3: window / door detection as framed voids in wall planes.           #
//##########################################################################

#pragma once

#include "Planes.h"

#include <QJsonArray>
#include <vector>

class ccPointCloud;

namespace Openings
{
	enum class Type
	{
		Door,
		Window,
		Unknown
	};

	//! A detected opening on a wall plane.
	struct Opening
	{
		int    wallIndex  = -1;
		Type   type       = Type::Unknown;
		double width      = 0.0;  //!< global units
		double height     = 0.0;  //!< global units
		double sillHeight = 0.0;  //!< bottom above wall base, global units
		double confidence = 0.0;  //!< 0..1 (fraction of surrounding frame occupied)

		// Rect in wall-plane LOCAL coords (for elevation drawing overlay).
		double u0 = 0.0, v0 = 0.0, u1 = 0.0, v1 = 0.0;
	};

	struct Params
	{
		double cellSize     = 0.0;  //!< 0 => auto (~0.08 global units)
		double minWidth     = 0.4;  //!< global units
		double minHeight    = 0.4;  //!< global units
		double doorSillMax  = 0.30; //!< sill <= this (and reaches floor) => door
		double minFrameOcc  = 0.5;  //!< min occupied fraction of the void's frame
		int    maxGridCells = 2000; //!< clamp per-axis grid resolution
	};

	//! Detects openings across all wall planes in \p model.
	std::vector<Opening> detect(const ccPointCloud* cloud,
	                            const Planes::Model& model,
	                            const Params&        params = Params());

	QJsonArray toJson(const std::vector<Opening>& openings, const QString& unit);
}
