// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                                                                        #
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//#                                                                        #
//#  Building dimension extraction core (shared by GUI action + CLI).      #
//#  Distributed under the terms of the GNU GPL v3 (see LICENSE).          #
//#                                                                        #
//##########################################################################

#pragma once

#include <QString>
#include <QStringList>

// CCCoreLib
#include <CCGeom.h>

class ccPointCloud;
class ccPolyline;
class ccHObject;

namespace BuildingDims
{
	//! Method used to derive the horizontal footprint (length x width).
	enum class Method
	{
		OBB,  //!< Oriented bounding box on the XY footprint (PCA). Default.
		AABB  //!< Axis-aligned bounding box (fast fallback).
	};

	//! Result of a dimension extraction.
	struct Result
	{
		bool valid = false;

		double length = 0.0;  //!< Longer horizontal edge (global units).
		double width  = 0.0;  //!< Shorter horizontal edge (global units).
		double height = 0.0;  //!< Z extent (global units).

		Method  method = Method::OBB;
		QString unit   = QStringLiteral("m");

		//! Footprint rectangle corners, in GLOBAL coordinates, CCW, at ground Z.
		CCVector3d footprint[4];

		//! Horizontal principal axes (local frame, unit vectors).
		CCVector3d axisLength;
		CCVector3d axisWidth;

		//! Centroid of the footprint, GLOBAL coordinates.
		CCVector3d center;

		//! Global shift/scale that was applied to convert to global coords.
		CCVector3d globalShift;
		double     globalScale = 1.0;

		unsigned   pointCount = 0;
		QStringList warnings;
	};

	//! Computes building dimensions from a point cloud.
	/** \param cloud   input cloud (not modified)
	    \param method  OBB (default) or AABB
	    \param unit    unit label carried into the result / JSON
	    \return result (check .valid)
	**/
	Result compute(const ccPointCloud* cloud,
	               Method               method = Method::OBB,
	               const QString&       unit   = QStringLiteral("m"));

	//! Builds a closed footprint polyline (owns its vertices) from a result.
	/** Coordinates are stored LOCAL to \p referenceCloud so the polyline lines
	    up with the source cloud in the 3D view; global shift/scale is copied
	    over so export round-trips to real-world coordinates.
	    \return new ccPolyline (caller owns it) or nullptr on failure
	**/
	ccPolyline* buildFootprintPolyline(const Result&       result,
	                                    const ccPointCloud* referenceCloud);

	//! Serializes a result to a JSON document (UTF-8 encoded).
	QByteArray toJson(const Result& result, const QString& sourceName = QString());

	//! Writes the JSON document to \p path. \return true on success.
	bool writeJson(const Result& result, const QString& path, const QString& sourceName = QString());

	//! Exports an entity tree to DXF using CloudCompare's bundled DXF filter.
	/** \return empty string on success, otherwise an error message. **/
	QString exportDxf(ccHObject* entities, const QString& path);
}
