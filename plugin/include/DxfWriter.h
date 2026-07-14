// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//#                                                                        #
//#  Minimal self-contained DXF (R12 ASCII) writer.                        #
//#  Emits LAYERs, LINEs, POLYLINEs, TEXT and "exploded" aligned           #
//#  dimensions (line + ticks + text) so plan / elevation drawings open    #
//#  in any CAD viewer without depending on CloudCompare's DXF filter.     #
//##########################################################################

#pragma once

#include <string>
#include <vector>

//! Tiny AutoCAD R12 DXF writer (2D). Coordinates are plain doubles.
class DxfWriter
{
public:
	struct Pt
	{
		double x = 0.0;
		double y = 0.0;
	};

	//! Registers a layer (name + AutoCAD color index; 7 = white/black).
	void addLayer(const std::string& name, int color = 7);

	void addLine(const std::string& layer, double x1, double y1, double x2, double y2);

	void addPolyline(const std::string& layer, const std::vector<Pt>& pts, bool closed);

	//! Single-line text anchored at (x,y), given height, optional rotation (deg).
	void addText(const std::string& layer, double x, double y, double height,
	             const std::string& text, double rotationDeg = 0.0);

	//! Aligned dimension between p1 and p2, drawn "exploded":
	//! a dimension line offset perpendicular by \p offset, extension lines,
	//! tick marks, and the \p label text at the midpoint.
	void addAlignedDim(const std::string& layer,
	                   double x1, double y1, double x2, double y2,
	                   double offset, const std::string& label, double textHeight);

	//! Writes the accumulated document to \p path. \return true on success.
	bool write(const std::string& path) const;

private:
	struct Layer
	{
		std::string name;
		int         color = 7;
	};

	std::vector<Layer> m_layers;
	std::string        m_entities;  //!< accumulated ENTITIES section body

	void code(int groupCode, const std::string& value);
	void code(int groupCode, double value);
	void code(int groupCode, int value);
};
