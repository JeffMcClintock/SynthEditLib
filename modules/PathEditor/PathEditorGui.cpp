// SPDX-License-Identifier: ISC
// Copyright 2026 Jeff McClintock.

#include "../Controls/ControlsBase.h"
#include <cstdio>

// ── Graphical path editor ───────────────────────────────────────────────────
// Persists a single SVG-subset "d" string (commands M, L, C, Z, absolute coords)
// in a private GUI parameter. Multiple subpaths supported. Cubic Bezier handles
// shown for the selected node. Pointer interaction:
//   left-click empty space        : append Line node to current subpath
//                                   (Shift to start a new subpath)
//   left-click anchor             : select + drag
//   left-click control handle     : drag handle
//   double-click segment          : insert node mid-segment (Bezier split)
//   Alt+left-click anchor         : toggle Line / Cubic kind
//   right-click anchor            : context menu (Delete / Convert / Close)
//   Delete / Backspace            : remove selected node
//   Escape                        : deselect

class PathEditorGui final : public ControlsBase
{
	enum class NodeKind { Line, Cubic };

	struct Node
	{
		Point    pos;
		Point    ctrlIn{};       // control point of incoming Cubic segment (paired with this node)
		Point    ctrlOut{};      // control point of outgoing Cubic segment (paired with this node)
		NodeKind kind = NodeKind::Line;  // kind of segment INTO this node; ignored if startsSubpath
		bool     startsSubpath = false;
		bool     closesSubpath = false;
	};

	Pin<std::string> pinPath;

	std::vector<Node> nodes;
	int  selectedIdx = -1;
	enum class Drag { None, Anchor, CtrlIn, CtrlOut, SmoothCreate } drag = Drag::None;
	bool suppressParseOnSelfWrite = false;

	static constexpr float kAnchorHalf      = 4.0f;   // anchor square half-size
	static constexpr float kHandleRadius    = 3.0f;
	static constexpr float kHitTolAnchor    = 6.0f;
	static constexpr float kHitTolHandle    = 5.0f;
	static constexpr float kHitTolSegment   = 4.0f;
	static constexpr float kHandleSeed      = 20.0f;

	// ── Geometry helpers ─────────────────────────────────────────────────────
	static Point lerp(Point a, Point b, float t)
	{
		return { a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t };
	}

	static float distSq(Point a, Point b)
	{
		const float dx = a.x - b.x, dy = a.y - b.y;
		return dx * dx + dy * dy;
	}

	static Point cubicEval(Point p0, Point p1, Point p2, Point p3, float t)
	{
		const Point q0 = lerp(p0, p1, t);
		const Point q1 = lerp(p1, p2, t);
		const Point q2 = lerp(p2, p3, t);
		const Point r0 = lerp(q0, q1, t);
		const Point r1 = lerp(q1, q2, t);
		return lerp(r0, r1, t);
	}

	// Distance from p to line segment a-b, with t along the segment in [0,1].
	static float distToLineSeg(Point p, Point a, Point b, float& tOut)
	{
		const float dx = b.x - a.x, dy = b.y - a.y;
		const float len2 = dx * dx + dy * dy;
		float t = 0.0f;
		if(len2 > 1e-6f)
			t = ((p.x - a.x) * dx + (p.y - a.y) * dy) / len2;
		t = (std::clamp)(t, 0.0f, 1.0f);
		tOut = t;
		const float qx = a.x + dx * t, qy = a.y + dy * t;
		const float ex = p.x - qx, ey = p.y - qy;
		return std::sqrt(ex * ex + ey * ey);
	}

	// Flatten cubic and find the closest point. Returns distance; writes t.
	static float distToCubic(Point p, Point p0, Point p1, Point p2, Point p3, float& tOut)
	{
		constexpr int N = 24;
		float bestDist = std::numeric_limits<float>::max();
		float bestT = 0.0f;
		Point prev = p0;
		for(int i = 1; i <= N; ++i)
		{
			const float tEnd = static_cast<float>(i) / N;
			const Point next = cubicEval(p0, p1, p2, p3, tEnd);
			float segT;
			const float d = distToLineSeg(p, prev, next, segT);
			if(d < bestDist)
			{
				bestDist = d;
				const float tStart = static_cast<float>(i - 1) / N;
				bestT = tStart + (tEnd - tStart) * segT;
			}
			prev = next;
		}
		tOut = bestT;
		return bestDist;
	}

	// ── Subpath helpers ──────────────────────────────────────────────────────
	int subpathStartOf(int idx) const
	{
		while(idx > 0 && !nodes[idx].startsSubpath)
			--idx;
		return idx;
	}

	int subpathEndOf(int idx) const
	{
		while(idx + 1 < static_cast<int>(nodes.size()) && !nodes[idx + 1].startsSubpath)
			++idx;
		return idx;
	}

	// ── Parse SVG-subset d-string into nodes[] ───────────────────────────────
	void parsePathString(const std::string& d)
	{
		nodes.clear();
		selectedIdx = -1;
		drag = Drag::None;

		struct Tok { char cmd; std::vector<float> args; };
		std::vector<Tok> tokens;
		for(auto p = d.c_str(); *p; ++p)
		{
			if(std::isalpha(static_cast<unsigned char>(*p)))
			{
				tokens.push_back({ *p });
			}
			else if(std::isdigit(static_cast<unsigned char>(*p)) || *p == '-' || *p == '.')
			{
				if(tokens.empty()) continue;
				char* endp = nullptr;
				tokens.back().args.push_back(static_cast<float>(std::strtod(p, &endp)));
				if(endp <= p) break;
				p = endp - 1;
			}
		}

		Point last{};
		Point first{};
		for(const auto& t : tokens)
		{
			switch(t.cmd)
			{
			case 'M': case 'm':
			{
				const bool rel = (t.cmd == 'm');
				if(t.args.size() < 2) break;
				Point p{ t.args[0], t.args[1] };
				if(rel && !nodes.empty()) { p.x += last.x; p.y += last.y; }
				Node n; n.pos = p; n.startsSubpath = true; n.kind = NodeKind::Line;
				nodes.push_back(n);
				last = first = p;
				for(size_t i = 1; i + 1 < t.args.size(); i += 2)
				{
					Point q{ t.args[i], t.args[i + 1] };
					if(rel) { q.x += last.x; q.y += last.y; }
					Node m; m.pos = q; m.kind = NodeKind::Line;
					nodes.push_back(m);
					last = q;
				}
			}
			break;

			case 'L': case 'l':
			{
				const bool rel = (t.cmd == 'l');
				for(size_t i = 0; i + 1 < t.args.size(); i += 2)
				{
					Point q{ t.args[i], t.args[i + 1] };
					if(rel) { q.x += last.x; q.y += last.y; }
					Node m; m.pos = q; m.kind = NodeKind::Line;
					nodes.push_back(m);
					last = q;
				}
			}
			break;

			case 'H': case 'h':
			{
				const bool rel = (t.cmd == 'h');
				for(float x : t.args)
				{
					Point q{ rel ? last.x + x : x, last.y };
					Node m; m.pos = q; m.kind = NodeKind::Line;
					nodes.push_back(m);
					last = q;
				}
			}
			break;

			case 'V': case 'v':
			{
				const bool rel = (t.cmd == 'v');
				for(float y : t.args)
				{
					Point q{ last.x, rel ? last.y + y : y };
					Node m; m.pos = q; m.kind = NodeKind::Line;
					nodes.push_back(m);
					last = q;
				}
			}
			break;

			case 'C': case 'c':
			{
				const bool rel = (t.cmd == 'c');
				for(size_t i = 0; i + 5 < t.args.size(); i += 6)
				{
					Point c1{ t.args[i],     t.args[i + 1] };
					Point c2{ t.args[i + 2], t.args[i + 3] };
					Point pe{ t.args[i + 4], t.args[i + 5] };
					if(rel)
					{
						c1.x += last.x; c1.y += last.y;
						c2.x += last.x; c2.y += last.y;
						pe.x += last.x; pe.y += last.y;
					}
					if(!nodes.empty())
						nodes.back().ctrlOut = c1;
					Node m;
					m.pos = pe;
					m.ctrlIn = c2;
					m.kind = NodeKind::Cubic;
					nodes.push_back(m);
					last = pe;
				}
			}
			break;

			case 'Z': case 'z':
				if(!nodes.empty())
				{
					nodes.back().closesSubpath = true;
					last = first;
				}
				break;

			default:
				break;
			}
		}

		// Detect cubic-close patterns: a subpath whose Z is preceded by a Cubic
		// whose endpoint coincides with the M point. Collapse the extra node
		// into M itself, encoding the close as "M.kind == Cubic" with the
		// cubic's second control stashed on M.ctrlIn.
		for(int i = static_cast<int>(nodes.size()) - 1; i > 0; --i)
		{
			auto& n = nodes[i];
			if(!n.closesSubpath || n.kind != NodeKind::Cubic || n.startsSubpath)
				continue;

			const int subStart = subpathStartOf(i);
			if(subStart == i) continue;
			auto& mNode = nodes[subStart];

			const float dx = n.pos.x - mNode.pos.x;
			const float dy = n.pos.y - mNode.pos.y;
			if(dx * dx + dy * dy > 0.01f) continue; // not coincident with M

			// Collapse: M absorbs the closing cubic's "second control" and the close flag.
			// nodes[i-1].ctrlOut already holds the first control point from the C parse.
			mNode.kind = NodeKind::Cubic;
			mNode.ctrlIn = n.ctrlIn;
			mNode.closesSubpath = true;
			nodes.erase(nodes.begin() + i);
		}
	}

	// ── Serialize nodes[] to SVG d-string ────────────────────────────────────
	static std::string fmtCoord(float v)
	{
		v = std::round(v * 10.0f) / 10.0f;
		char buf[32];
		std::snprintf(buf, sizeof(buf), "%g", v);
		return buf;
	}

	std::string serializePath() const
	{
		std::string out;
		for(size_t i = 0; i < nodes.size(); ++i)
		{
			const auto& n = nodes[i];
			if(!out.empty()) out += ' ';

			if(n.startsSubpath)
			{
				out += "M " + fmtCoord(n.pos.x) + ' ' + fmtCoord(n.pos.y);
			}
			else if(n.kind == NodeKind::Cubic)
			{
				const auto& prev = nodes[i - 1];
				out += "C "
					+ fmtCoord(prev.ctrlOut.x) + ' ' + fmtCoord(prev.ctrlOut.y) + ' '
					+ fmtCoord(n.ctrlIn.x)     + ' ' + fmtCoord(n.ctrlIn.y)     + ' '
					+ fmtCoord(n.pos.x)        + ' ' + fmtCoord(n.pos.y);
			}
			else // Line
			{
				out += "L " + fmtCoord(n.pos.x) + ' ' + fmtCoord(n.pos.y);
			}

			if(n.closesSubpath)
			{
				// Cubic close: M's `kind == Cubic` means the closing segment is a
				// Bezier. Emit it explicitly before Z, controls: (last.ctrlOut, M.ctrlIn).
				const int subStart = subpathStartOf(static_cast<int>(i));
				const auto& mNode = nodes[subStart];
				if(subStart != static_cast<int>(i) && mNode.kind == NodeKind::Cubic)
				{
					out += " C "
						+ fmtCoord(n.ctrlOut.x)    + ' ' + fmtCoord(n.ctrlOut.y)    + ' '
						+ fmtCoord(mNode.ctrlIn.x) + ' ' + fmtCoord(mNode.ctrlIn.y) + ' '
						+ fmtCoord(mNode.pos.x)    + ' ' + fmtCoord(mNode.pos.y);
				}
				out += " Z";
			}
		}
		return out;
	}

	void commit()
	{
		suppressParseOnSelfWrite = true;
		pinPath = serializePath();
		suppressParseOnSelfWrite = false;
	}

	// ── Hit testing ──────────────────────────────────────────────────────────
	int hitAnchor(Point p) const
	{
		const float tolSq = kHitTolAnchor * kHitTolAnchor;
		// Iterate in reverse so a recently-added node wins over an underlying one.
		for(int i = static_cast<int>(nodes.size()) - 1; i >= 0; --i)
			if(distSq(p, nodes[i].pos) <= tolSq)
				return i;
		return -1;
	}

	// Which of a node's two control handles should be visible / hit-testable.
	// Accounts for the "cubic close" case where M itself has an incoming cubic
	// (back from the last node in a closed subpath) and the last node has an
	// outgoing cubic forward to M.
	struct HandleVisibility { bool hasIn, hasOut; };
	HandleVisibility computeHandles(int idx) const
	{
		HandleVisibility v{ false, false };
		if(idx < 0 || idx >= static_cast<int>(nodes.size())) return v;

		const auto& n      = nodes[idx];
		const int subStart = subpathStartOf(idx);
		const int subEnd   = subpathEndOf(idx);
		const bool closed  = nodes[subEnd].closesSubpath;
		const bool cubicClose = closed && (subStart != subEnd)
		                       && nodes[subStart].kind == NodeKind::Cubic;

		// Incoming side: regular cubic-into-node OR M with cubic-close.
		if(n.startsSubpath)
			v.hasIn = cubicClose;
		else
			v.hasIn = (n.kind == NodeKind::Cubic);

		// Outgoing side: cubic-into-next OR last-node with cubic-close.
		if(idx < subEnd)
			v.hasOut = (nodes[idx + 1].kind == NodeKind::Cubic);
		else
			v.hasOut = cubicClose;  // idx == subEnd

		return v;
	}

	// Check selected node's visible handles.
	bool hitSelectedHandle(Point p, Drag& which) const
	{
		if(selectedIdx < 0) return false;
		const float tolSq = kHitTolHandle * kHitTolHandle;
		const auto& n = nodes[selectedIdx];
		const auto vis = computeHandles(selectedIdx);

		if(vis.hasIn  && distSq(p, n.ctrlIn ) <= tolSq) { which = Drag::CtrlIn;  return true; }
		if(vis.hasOut && distSq(p, n.ctrlOut) <= tolSq) { which = Drag::CtrlOut; return true; }
		return false;
	}

	// Returns the *end* index of the hit segment (i.e. node[end] is reached by the segment).
	// Writes the parameter t in [0,1] along the segment.
	int hitSegment(Point p, float& tOut) const
	{
		int best = -1;
		float bestDist = kHitTolSegment;
		float bestT = 0.0f;

		for(int i = 1; i < static_cast<int>(nodes.size()); ++i)
		{
			if(nodes[i].startsSubpath) continue;
			const auto& a = nodes[i - 1];
			const auto& b = nodes[i];
			float t;
			float d;
			if(b.kind == NodeKind::Cubic)
				d = distToCubic(p, a.pos, a.ctrlOut, b.ctrlIn, b.pos, t);
			else
				d = distToLineSeg(p, a.pos, b.pos, t);

			if(d < bestDist)
			{
				bestDist = d;
				best = i;
				bestT = t;
			}
		}
		tOut = bestT;
		return best;
	}

	// ── Edit operations ──────────────────────────────────────────────────────
	void appendNode(Point p, bool startNewSubpath)
	{
		// If the trailing subpath is closed, an appended node implicitly
		// starts a new subpath (we can't extend a closed shape without
		// reopening it, and emitting "Z L x y" produces an invalid d-string).
		const bool tailClosed = !nodes.empty() && nodes.back().closesSubpath;

		Node n;
		n.pos = p;
		n.kind = NodeKind::Line;
		n.startsSubpath = startNewSubpath || nodes.empty() || tailClosed;
		nodes.push_back(n);
		selectedIdx = static_cast<int>(nodes.size()) - 1;
	}

	void insertOnSegment(int endIdx, float t)
	{
		if(endIdx <= 0 || endIdx >= static_cast<int>(nodes.size())) return;
		auto& a = nodes[endIdx - 1];
		auto& b = nodes[endIdx];

		Node n;
		if(b.kind == NodeKind::Cubic)
		{
			const Point p0 = a.pos;
			const Point p1 = a.ctrlOut;
			const Point p2 = b.ctrlIn;
			const Point p3 = b.pos;
			const Point q0 = lerp(p0, p1, t);
			const Point q1 = lerp(p1, p2, t);
			const Point q2 = lerp(p2, p3, t);
			const Point r0 = lerp(q0, q1, t);
			const Point r1 = lerp(q1, q2, t);
			const Point s  = lerp(r0, r1, t);

			a.ctrlOut = q0;
			n.pos = s;
			n.ctrlIn = r0;
			n.ctrlOut = r1;
			n.kind = NodeKind::Cubic;
			b.ctrlIn = q2;  // b.kind stays Cubic
		}
		else
		{
			n.pos = lerp(a.pos, b.pos, t);
			n.kind = NodeKind::Line;
		}

		nodes.insert(nodes.begin() + endIdx, n);
		selectedIdx = endIdx;
	}

	void deleteNode(int idx)
	{
		if(idx < 0 || idx >= static_cast<int>(nodes.size())) return;

		const bool wasStart  = nodes[idx].startsSubpath;
		const bool wasClose  = nodes[idx].closesSubpath;
		const bool hasNext   = (idx + 1 < static_cast<int>(nodes.size())) && !nodes[idx + 1].startsSubpath;

		nodes.erase(nodes.begin() + idx);

		// If we removed the start of a subpath, the next node (if still in that subpath) becomes the new start.
		if(wasStart && hasNext)
		{
			nodes[idx].startsSubpath = true;
			nodes[idx].kind = NodeKind::Line;  // an M-node's incoming kind is irrelevant
		}
		// If we removed a closing node, push the close flag onto the preceding node (if still in subpath).
		if(wasClose && idx > 0 && (idx >= static_cast<int>(nodes.size()) || nodes[idx].startsSubpath))
		{
			if(!nodes[idx - 1].startsSubpath || idx - 1 == 0 /* lone-M edge case: leave alone */)
				nodes[idx - 1].closesSubpath = true;
		}

		selectedIdx = -1;
		drag = Drag::None;
	}

	void toggleKind(int idx)
	{
		if(idx < 0 || idx >= static_cast<int>(nodes.size())) return;

		// A subpath-start node has no incoming segment; redirect the toggle to
		// the OUTGOING segment instead (so Alt+click on M works intuitively).
		if(nodes[idx].startsSubpath)
		{
			const int endIdx = subpathEndOf(idx);
			if(idx >= endIdx) return;  // lone M, no segment to toggle
			toggleKind(idx + 1);
			return;
		}

		auto& n = nodes[idx];
		auto& prev = nodes[idx - 1];

		if(n.kind == NodeKind::Line)
		{
			// Seed handles along the line direction.
			const float dx = n.pos.x - prev.pos.x;
			const float dy = n.pos.y - prev.pos.y;
			const float len = std::sqrt(dx * dx + dy * dy);
			const float ux = (len > 1e-3f) ? dx / len : 1.0f;
			const float uy = (len > 1e-3f) ? dy / len : 0.0f;
			const float off = (std::min)(kHandleSeed, len * 0.33f);
			prev.ctrlOut = { prev.pos.x + ux * off, prev.pos.y + uy * off };
			n.ctrlIn     = { n.pos.x    - ux * off, n.pos.y    - uy * off };
			n.kind = NodeKind::Cubic;
		}
		else
		{
			n.kind = NodeKind::Line;
		}
	}

	void toggleClose(int idx)
	{
		if(idx < 0 || idx >= static_cast<int>(nodes.size())) return;
		// Only the *last* node in a subpath should carry the Z.
		const int endIdx = subpathEndOf(idx);
		auto& last = nodes[endIdx];
		last.closesSubpath = !last.closesSubpath;
	}

	// Toggle the closing segment between Line and Cubic. The closing-segment
	// kind is stored on the subpath's M node (M.kind, normally ignored). When
	// promoting to Cubic we seed M.ctrlIn and lastNode.ctrlOut along the segment.
	void toggleCloseKind(int idx)
	{
		if(idx < 0 || idx >= static_cast<int>(nodes.size())) return;
		const int subStart = subpathStartOf(idx);
		const int subEnd   = subpathEndOf(idx);
		if(subStart == subEnd) return;             // lone M, no closing segment
		if(!nodes[subEnd].closesSubpath) return;   // not closed

		auto& mNode    = nodes[subStart];
		auto& lastNode = nodes[subEnd];

		if(mNode.kind == NodeKind::Line)
		{
			// Seed handles along the line from last → M.
			const float dx = mNode.pos.x - lastNode.pos.x;
			const float dy = mNode.pos.y - lastNode.pos.y;
			const float len = std::sqrt(dx * dx + dy * dy);
			const float ux = (len > 1e-3f) ? dx / len : 1.0f;
			const float uy = (len > 1e-3f) ? dy / len : 0.0f;
			const float off = (std::min)(kHandleSeed, len * 0.33f);
			lastNode.ctrlOut = { lastNode.pos.x + ux * off, lastNode.pos.y + uy * off };
			mNode.ctrlIn     = { mNode.pos.x    - ux * off, mNode.pos.y    - uy * off };
			mNode.kind = NodeKind::Cubic;
		}
		else
		{
			mNode.kind = NodeKind::Line;
		}
	}

	// ── Rendering ────────────────────────────────────────────────────────────
	PathGeometry buildPath(Graphics& g) const
	{
		auto geom = g.getFactory().createPathGeometry();
		auto sink = geom.open();

		int subpathStart = -1;
		bool inFigure = false;
		bool closeOnEnd = false;

		// Close current figure, emitting a closing cubic first when the
		// subpath's M is marked Cubic (cubic close).
		auto endCurrentFigure = [&](int onePastLast)
		{
			if(!inFigure) return;
			if(closeOnEnd && subpathStart >= 0 && subpathStart < onePastLast - 1
			   && nodes[subpathStart].kind == NodeKind::Cubic)
			{
				const auto& mNode    = nodes[subpathStart];
				const auto& lastNode = nodes[onePastLast - 1];
				sink.addBezier({ lastNode.ctrlOut, mNode.ctrlIn, mNode.pos });
			}
			sink.endFigure(closeOnEnd ? FigureEnd::Closed : FigureEnd::Open);
			inFigure = false;
			closeOnEnd = false;
		};

		for(size_t i = 0; i < nodes.size(); ++i)
		{
			const auto& n = nodes[i];
			if(n.startsSubpath)
			{
				endCurrentFigure(static_cast<int>(i));
				sink.beginFigure(n.pos, FigureBegin::Hollow);
				inFigure = true;
				subpathStart = static_cast<int>(i);
			}
			else if(n.kind == NodeKind::Cubic)
			{
				const auto& prev = nodes[i - 1];
				sink.addBezier({ prev.ctrlOut, n.ctrlIn, n.pos });
			}
			else
			{
				sink.addLine(n.pos);
			}
			if(n.closesSubpath)
				closeOnEnd = true;
		}
		endCurrentFigure(static_cast<int>(nodes.size()));
		sink.close();
		return geom;
	}

public:
	PathEditorGui()
	{
		pinPath.onUpdate = [this](PinBase*)
		{
			if(!suppressParseOnSelfWrite)
				parsePathString(pinPath.value);
			redraw();
		};
	}

	ReturnCode render(drawing::api::IDeviceContext* drawingContext) override
	{
		Graphics g(drawingContext);
		ClipDrawingToBounds _(g, bounds);

		const float w = getWidth(bounds);
		const float h = getHeight(bounds);
		const Rect bodyRect{ 0.0f, 0.0f, w, h };

		// Background + border
		auto bgBrush     = g.createSolidColorBrush(Colors::White);
		auto borderBrush = g.createSolidColorBrush(Color{ 0.6f, 0.6f, 0.6f, 1.0f });
		g.fillRectangle(bodyRect, bgBrush);
		g.drawRectangle(bodyRect, borderBrush, 1.0f);

		// Path itself
		if(!nodes.empty())
		{
			auto pathBrush = g.createSolidColorBrush(Color{ 0.1f, 0.1f, 0.1f, 1.0f });
			auto geom = buildPath(g);
			g.drawGeometry(geom, pathBrush, 1.5f);
		}

		// Anchors (drawn before handles so handles overlap them — matches the
		// hit-test order: hitSelectedHandle is consulted before hitAnchor).
		{
			auto whiteBrush = g.createSolidColorBrush(Colors::White);
			auto greyBrush  = g.createSolidColorBrush(Color{ 0.4f, 0.4f, 0.4f, 1.0f });
			auto blueBrush  = g.createSolidColorBrush(Color{ 0.18f, 0.47f, 0.78f, 1.0f });

			for(int i = 0; i < static_cast<int>(nodes.size()); ++i)
			{
				const auto& n = nodes[i];
				const bool selected = (i == selectedIdx);

				if(n.startsSubpath)
				{
					// open circle for subpath start
					if(selected)
						g.fillCircle(n.pos, kAnchorHalf, blueBrush);
					else
						g.fillCircle(n.pos, kAnchorHalf, whiteBrush);
					g.drawCircle(n.pos, kAnchorHalf, greyBrush, 1.0f);
				}
				else
				{
					const Rect r{
						n.pos.x - kAnchorHalf, n.pos.y - kAnchorHalf,
						n.pos.x + kAnchorHalf, n.pos.y + kAnchorHalf
					};
					if(selected)
						g.fillRectangle(r, blueBrush);
					else
						g.fillRectangle(r, whiteBrush);
					g.drawRectangle(r, greyBrush, 1.0f);
				}
			}
		}

		// Selected node's control handles on top of the anchors.
		if(selectedIdx >= 0 && selectedIdx < static_cast<int>(nodes.size()))
		{
			const auto& n = nodes[selectedIdx];
			auto handleBrush = g.createSolidColorBrush(Color{ 0.55f, 0.55f, 0.55f, 1.0f });
			const auto vis = computeHandles(selectedIdx);

			if(vis.hasIn)
			{
				g.drawLine(n.pos, n.ctrlIn, handleBrush, 1.0f);
				g.fillCircle(n.ctrlIn, kHandleRadius, handleBrush);
			}
			if(vis.hasOut)
			{
				g.drawLine(n.pos, n.ctrlOut, handleBrush, 1.0f);
				g.fillCircle(n.ctrlOut, kHandleRadius, handleBrush);
			}
		}

		return ReturnCode::Ok;
	}

	// ── Pointer ──────────────────────────────────────────────────────────────
	ReturnCode onPointerDown(Point point, int32_t flags) override
	{
		const bool first  = (flags & static_cast<int32_t>(gmpi::api::PointerFlags::FirstButton)) != 0;
		const bool second = (flags & static_cast<int32_t>(gmpi::api::PointerFlags::SecondButton)) != 0;
		const bool shift  = (flags & static_cast<int32_t>(gmpi::api::PointerFlags::KeyShift))    != 0;
		const bool alt    = (flags & static_cast<int32_t>(gmpi::api::PointerFlags::KeyAlt))      != 0;
		const bool dbl    = (flags & static_cast<int32_t>(gmpi::api::PointerFlags::Double))      != 0;

		// Right-click is reserved for the context menu (host calls populateContextMenu).
		if(second)
		{
			// Update selection so the context menu acts on the clicked node.
			const int hitIdx = hitAnchor(point);
			if(hitIdx >= 0)
			{
				selectedIdx = hitIdx;
				redraw();
			}
			return ReturnCode::Unhandled;
		}

		if(!first) return ReturnCode::Unhandled;

		// 1. Selected node's handle?
		{
			Drag which;
			if(hitSelectedHandle(point, which))
			{
				drag = which;
				pointPrevious = point;
				if(inputHost.get()) inputHost->setCapture();
				return ReturnCode::Ok;
			}
		}

		// 2. Any anchor?
		{
			const int idx = hitAnchor(point);
			if(idx >= 0)
			{
				if(alt) { toggleKind(idx); commit(); redraw(); return ReturnCode::Ok; }
				selectedIdx = idx;
				drag = Drag::Anchor;
				pointPrevious = point;
				if(inputHost.get()) inputHost->setCapture();
				redraw();
				return ReturnCode::Ok;
			}
		}

		// 3. Segment? (double-click splits)
		if(dbl)
		{
			float t;
			const int endIdx = hitSegment(point, t);
			if(endIdx > 0)
			{
				insertOnSegment(endIdx, t);
				drag = Drag::Anchor;
				pointPrevious = point;
				if(inputHost.get()) inputHost->setCapture();
				commit();
				redraw();
				return ReturnCode::Ok;
			}
		}

		// 4. Empty space — append a new node (or start a new subpath with Shift).
		// Click-and-drag from here lets the user define a smooth cubic node
		// (drag direction sets ctrlOut, mirror sets ctrlIn) — like a Pen tool.
		appendNode(point, shift);
		drag = Drag::SmoothCreate;
		pointPrevious = point;
		if(inputHost.get()) inputHost->setCapture();
		commit();
		redraw();
		return ReturnCode::Ok;
	}

	Point pointPrevious{};

	ReturnCode onPointerMove(Point point, int32_t /*flags*/) override
	{
		if(!hasCapture() || drag == Drag::None) return ReturnCode::Unhandled;
		if(selectedIdx < 0 || selectedIdx >= static_cast<int>(nodes.size()))
			return ReturnCode::Unhandled;

		auto& n = nodes[selectedIdx];

		// Pen-tool smooth create: the just-appended anchor stays put; the
		// drag-end defines ctrlOut, with ctrlIn as its mirror around the anchor.
		// The incoming segment (if any) becomes cubic.
		if(drag == Drag::SmoothCreate)
		{
			n.ctrlOut = point;
			n.ctrlIn  = { 2.0f * n.pos.x - point.x, 2.0f * n.pos.y - point.y };

			if(!n.startsSubpath && selectedIdx > 0)
			{
				n.kind = NodeKind::Cubic;
				auto& prev = nodes[selectedIdx - 1];
				// Seed prev.ctrlOut with a sensible default if it's never been set.
				// Treat (0,0) as unset — a reasonable assumption for our coord space.
				if(prev.ctrlOut.x == 0.0f && prev.ctrlOut.y == 0.0f)
					prev.ctrlOut = lerp(prev.pos, n.pos, 0.33f);
			}

			commit();
			redraw();
			return ReturnCode::Ok;
		}

		const float dx = point.x - pointPrevious.x;
		const float dy = point.y - pointPrevious.y;
		pointPrevious = point;

		switch(drag)
		{
		case Drag::Anchor:
			n.pos.x     += dx; n.pos.y     += dy;
			n.ctrlIn.x  += dx; n.ctrlIn.y  += dy;
			n.ctrlOut.x += dx; n.ctrlOut.y += dy;
			break;
		case Drag::CtrlIn:
			n.ctrlIn.x  += dx; n.ctrlIn.y  += dy;
			break;
		case Drag::CtrlOut:
			n.ctrlOut.x += dx; n.ctrlOut.y += dy;
			break;
		default: break;
		}

		commit();
		redraw();
		return ReturnCode::Ok;
	}

	ReturnCode onPointerUp(Point /*point*/, int32_t /*flags*/) override
	{
		if(!hasCapture()) return ReturnCode::Unhandled;
		if(inputHost.get()) inputHost->releaseCapture();
		drag = Drag::None;
		return ReturnCode::Ok;
	}

	// ── Context menu ─────────────────────────────────────────────────────────
	ReturnCode populateContextMenu(Point /*point*/, gmpi::api::IUnknown* contextMenuItemsSink) override
	{
		gmpi::shared_ptr<gmpi::api::IUnknown> unknown;
		unknown = contextMenuItemsSink;
		auto sink = unknown.as<gmpi::api::IContextItemSink>();
		if(!sink) return ReturnCode::Fail;

		ContextMenuHelper menu(sink.get());

		if(selectedIdx >= 0 && selectedIdx < static_cast<int>(nodes.size()))
		{
			const int idx = selectedIdx;
			const auto& n = nodes[idx];
			const bool isStart = n.startsSubpath;

			menu.addItem("Delete Node", 0, [this, idx](int32_t) { deleteNode(idx); commit(); redraw(); });

			if(!isStart)
			{
				const char* label = (n.kind == NodeKind::Line) ? "Convert to Cubic" : "Convert to Line";
				menu.addItem(label, 0, [this, idx](int32_t) { toggleKind(idx); commit(); redraw(); });
			}
			else if(idx + 1 < static_cast<int>(nodes.size()) && !nodes[idx + 1].startsSubpath)
			{
				// Subpath-start node has no incoming segment, but it does have an
				// outgoing one whose kind we can toggle.
				const char* label = (nodes[idx + 1].kind == NodeKind::Line)
					? "Convert Next Segment to Cubic"
					: "Convert Next Segment to Line";
				menu.addItem(label, 0, [this, idx](int32_t) { toggleKind(idx); commit(); redraw(); });
			}

			const int endIdx = subpathEndOf(idx);
			const bool isClosed = nodes[endIdx].closesSubpath;
			menu.addItem(isClosed ? "Open Subpath" : "Close Subpath", 0,
				[this, idx](int32_t) { toggleClose(idx); commit(); redraw(); });

			// Cubic-close: kind is stored on the subpath's M node.
			if(isClosed && subpathStartOf(idx) != endIdx)
			{
				const auto& mNode = nodes[subpathStartOf(idx)];
				const char* label = (mNode.kind == NodeKind::Line)
					? "Convert Closing Segment to Cubic"
					: "Convert Closing Segment to Line";
				menu.addItem(label, 0, [this, idx](int32_t) { toggleCloseKind(idx); commit(); redraw(); });
			}

			return ReturnCode::Ok;
		}

		menu.addItem("Clear All", 0, [this](int32_t) { nodes.clear(); selectedIdx = -1; commit(); redraw(); });
		return ReturnCode::Ok;
	}

	// ── Keyboard ─────────────────────────────────────────────────────────────
	ReturnCode onKeyPress(wchar_t c) override
	{
		switch(c)
		{
		case 0x7F: // DEL
		case 0x08: // Backspace
			if(selectedIdx >= 0)
			{
				deleteNode(selectedIdx);
				commit();
				redraw();
				return ReturnCode::Ok;
			}
			break;
		case 0x1B: // Escape
			if(selectedIdx >= 0)
			{
				selectedIdx = -1;
				redraw();
				return ReturnCode::Ok;
			}
			break;
		default: break;
		}
		return ReturnCode::Unhandled;
	}
};

namespace
{
auto r = gmpi::Register<PathEditorGui>::withXml(R"XML(
<?xml version="1.0" encoding="UTF-8"?>
<Plugin id="SE Path Editor" name="Path Editor" category="Sub-Controls">
    <GUI graphicsApi="GmpiGui">
        <Pin name="Path" datatype="string"/>
    </GUI>
</Plugin>
)XML");
}
