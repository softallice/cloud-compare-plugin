// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//#                                                                        #
//#  L2: self-contained RANSAC plane extraction + wall/floor/roof          #
//#  classification and storey estimation.                                 #
//##########################################################################

#pragma once

#include <CCGeom.h>

#include <QJsonObject>
#include <vector>

class ccPointCloud;

namespace Planes
{
	enum class Type
	{
		Wall,
		Floor,
		Roof,
		Other
	};

	//! A detected planar region.
	struct Plane
	{
		Type       type = Type::Other;
		CCVector3d normal;          //!< unit normal (local frame)
		CCVector3d centroidGlobal;  //!< centroid in global coordinates
		double     inPlaneWidth  = 0.0;  //!< global units
		double     inPlaneHeight = 0.0;  //!< global units
		double     elevationZ    = 0.0;  //!< global Z of centroid (useful for horizontals)
		unsigned   inlierCount   = 0;
		std::vector<unsigned> inliers;   //!< indices into the source cloud
	};

	struct Model
	{
		std::vector<Plane> planes;
		int    storeyCount  = 0;
		double storeyHeight = 0.0;  //!< median floor-to-floor gap (global units)
	};

	struct Params
	{
		double   distThreshold   = 0.0;   //!< 0 => auto (0.5% of bbox diagonal)
		double   minInlierFrac   = 0.02;  //!< min inliers as a fraction of cloud size
		int      maxPlanes       = 12;
		double   minStoreyGap    = 2.0;   //!< global units; separates horizontal levels
		unsigned ransacIters     = 200;   //!< iterations per plane
		unsigned seed            = 12345;
	};

	//! Detects planes and classifies them. \p cloud is not modified.
	Model detect(const ccPointCloud* cloud, const Params& params = Params());

	//! Serializes a model into a JSON object (keys: planes[], storey_count, storey_height).
	QJsonObject toJson(const Model& model, const QString& unit);
}
