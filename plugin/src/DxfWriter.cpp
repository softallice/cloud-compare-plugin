// SPDX-License-Identifier: GPL-3.0-or-later
//##########################################################################
//#                CLOUDCOMPARE PLUGIN: qBuildingDims                       #
//##########################################################################

// MSVC does not define M_PI unless this is set before any <cmath>/<math.h>.
#define _USE_MATH_DEFINES

#include "DxfWriter.h"

#include <cmath>
#include <cstdio>
#include <fstream>

namespace
{
	// DXF group-code/value line: "%code\n%value\n".
	std::string line(int groupCode, const std::string& value)
	{
		char head[16];
		std::snprintf(head, sizeof(head), "%d\n", groupCode);
		return std::string(head) + value + "\n";
	}

	std::string num(double v)
	{
		char buf[64];
		std::snprintf(buf, sizeof(buf), "%.6f", v);
		return std::string(buf);
	}
}

void DxfWriter::code(int groupCode, const std::string& value)
{
	m_entities += line(groupCode, value);
}

void DxfWriter::code(int groupCode, double value)
{
	m_entities += line(groupCode, num(value));
}

void DxfWriter::code(int groupCode, int value)
{
	m_entities += line(groupCode, std::to_string(value));
}

void DxfWriter::addLayer(const std::string& name, int color)
{
	for (const Layer& l : m_layers)
	{
		if (l.name == name)
			return;
	}
	m_layers.push_back({ name, color });
}

void DxfWriter::addLine(const std::string& layer, double x1, double y1, double x2, double y2)
{
	addLayer(layer);
	code(0, std::string("LINE"));
	code(8, layer);
	code(10, x1);
	code(20, y1);
	code(30, 0.0);
	code(11, x2);
	code(21, y2);
	code(31, 0.0);
}

void DxfWriter::addPolyline(const std::string& layer, const std::vector<Pt>& pts, bool closed)
{
	if (pts.empty())
		return;
	addLayer(layer);

	code(0, std::string("POLYLINE"));
	code(8, layer);
	code(66, 1);          // vertices-follow flag
	code(70, closed ? 1 : 0);
	for (const Pt& p : pts)
	{
		code(0, std::string("VERTEX"));
		code(8, layer);
		code(10, p.x);
		code(20, p.y);
		code(30, 0.0);
	}
	code(0, std::string("SEQEND"));
	code(8, layer);
}

void DxfWriter::addText(const std::string& layer, double x, double y, double height,
                        const std::string& text, double rotationDeg)
{
	addLayer(layer);
	code(0, std::string("TEXT"));
	code(8, layer);
	code(10, x);
	code(20, y);
	code(30, 0.0);
	code(40, height);
	code(1, text);
	if (rotationDeg != 0.0)
		code(50, rotationDeg);
}

void DxfWriter::addAlignedDim(const std::string& layer,
                              double x1, double y1, double x2, double y2,
                              double offset, const std::string& label, double textHeight)
{
	addLayer(layer);

	const double dx = x2 - x1;
	const double dy = y2 - y1;
	const double len = std::sqrt(dx * dx + dy * dy);
	if (len < 1e-9)
		return;

	// Unit direction along the measured segment and its perpendicular.
	const double ux = dx / len;
	const double uy = dy / len;
	const double px = -uy;  // perpendicular (offset direction)
	const double py = ux;

	// Dimension line endpoints (offset from the measured points).
	const double a1x = x1 + px * offset;
	const double a1y = y1 + py * offset;
	const double a2x = x2 + px * offset;
	const double a2y = y2 + py * offset;

	// Extension lines (from measured points to the dimension line).
	addLine(layer, x1, y1, a1x, a1y);
	addLine(layer, x2, y2, a2x, a2y);
	// Dimension line.
	addLine(layer, a1x, a1y, a2x, a2y);

	// Tick marks (short 45-deg strokes at each end).
	const double t = textHeight * 0.6;
	addLine(layer, a1x - (ux + px) * t, a1y - (uy + py) * t,
	        a1x + (ux + px) * t, a1y + (uy + py) * t);
	addLine(layer, a2x - (ux + px) * t, a2y - (uy + py) * t,
	        a2x + (ux + px) * t, a2y + (uy + py) * t);

	// Label at the midpoint, nudged off the line, rotated to match.
	const double mx = (a1x + a2x) * 0.5 + px * textHeight * 0.4;
	const double my = (a1y + a2y) * 0.5 + py * textHeight * 0.4;
	const double rot = std::atan2(uy, ux) * 180.0 / M_PI;
	addText(layer, mx, my, textHeight, label, rot);
}

bool DxfWriter::write(const std::string& path) const
{
	std::ofstream f(path, std::ios::out | std::ios::trunc);
	if (!f)
		return false;

	// --- TABLES: layer table -------------------------------------------------
	f << line(0, "SECTION") << line(2, "TABLES") << line(0, "TABLE") << line(2, "LAYER");
	f << line(70, std::to_string(static_cast<int>(m_layers.size()) + 1));
	// default layer "0"
	f << line(0, "LAYER") << line(2, "0") << line(70, "0") << line(62, "7") << line(6, "CONTINUOUS");
	for (const Layer& l : m_layers)
	{
		f << line(0, "LAYER") << line(2, l.name) << line(70, "0")
		  << line(62, std::to_string(l.color)) << line(6, "CONTINUOUS");
	}
	f << line(0, "ENDTAB") << line(0, "ENDSEC");

	// --- ENTITIES ------------------------------------------------------------
	f << line(0, "SECTION") << line(2, "ENTITIES");
	f << m_entities;
	f << line(0, "ENDSEC");

	// --- EOF -----------------------------------------------------------------
	f << line(0, "EOF");

	return static_cast<bool>(f);
}
