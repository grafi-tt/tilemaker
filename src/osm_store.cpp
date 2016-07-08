/*
	OSM Store

	Store all of those to be output: latp/lon for nodes, node list for ways, and way list for relations.
	Only one instance of OSMStore is ever used. It will serve as the global data store. All data determined
	to be output will be set here, from tilemaker.cpp.

	OSMStore will be mainly used for geometry generation. Geometry generation logic is implemented in this class.
	These functions are used by osm_output, and can be used by osm_object to provide the geometry information to Lua.

	Internal data structures are encapsulated in NodeStore, WayStore and RelationStore classes.
	These store can be altered for efficient memory use without global code changes.
	Such data structures have to return const ForwardInputIterators (only *, ++ and == should be supported).

	Possible future improvements to save memory:
	- pack WayStore (e.g. zigzag PBF encoding and varint)
	- combine innerWays and outerWays into one vector, with a single-byte index marking the changeover
	- use two arrays (sorted keys and elements) instead of map
*/

//
// Views of data structures.
//

// A NodeList is a list of nodes in a way
template<class NodeIt>
struct NodeList {
	NodeIt begin;
	NodeIt end;
};

NodeList<NodeVec::const_iterator> makeNodeList(const NodeVec &nodeVec) {
	return { nodeVec.cbegin(), nodeVec.cend() };
}

// A WayList is a list of ways in a multipolygon relation, arranged to constitute a multipolygon
// (currently non-multipolygon relation is unsupported).
//
// Pseudo OSM ids PSEUDO_WAY_OUTER_MARK, PSEUDO_WAY_INNER_MARK, and PSEUDO_WAY_REVERSE_MARK are used
// to represent the multipolygon structure.
// PSEUDO_WAY_OUTER_MARK denotes: a switch to another polygon;
// PSEUDO_WAY_INNER_MARK denotes: a switch to another interior ring, inside the current polygon; and
// PSEUDO_WAY_REVERSE_MARK denotes: that current way is connected reversely.
//
// Example:
//   - Polygon
//      - Outer Ring
//        - 1, reversed-2, 3, 1
//      - Inner Rings
//        - 4, 5, reversed-6, 4
//        - 7, 8, 9, 10, 7
//   - Polygon
//     - Outer Ring
//       - 11, 12, 13, 11
// is encoded as
//   [1, REVERSE_MARK, 2, 3, 1, INNER_MARK, 4, 5, REVERSE_MARK, 6, 4, INNER_MARK, 7, 8, 9, 10, 7, OUTER_MARK, 11, 12, 13, 11]
constexpr WayID PSEUDO_WAY_OUTER_MARK = static_cast<WayID>(-1); // max value, 0xFFFF...
constexpr WayID PSEUDO_WAY_INNER_MARK = static_cast<WayID>(-1)-1;
constexpr WayID PSEUDO_WAY_REVERSE_MARK = static_cast<WayID>(-1)-2;

template<class WayIt>
struct WayList {
	WayIt begin;
	WayIt end;
};

WayList<WayVec::const_iterator> makeWayList(const WayVec &wayVec) {
	return { wayVec.cbegin(), wayVec.cend() };
}

//
// Internal data structures.
//

// node store
class NodeStore {
	std::unordered_map<NodeID, LatpLon> mLatpLons;

public:
	// @brief Lookup a latp/lon pair
	// @param i OSM ID of a node
	// @return Latp/lon pair
	// @exception NotFound
	LatpLon at(NodeID i) const {
		return mLatpLons.at(i);
	}

	// @brief Return whether a latp/lon pair is on the store.
	// @param i Any possible OSM ID
	// @return 1 if found, 0 otherwise
	// @note This function is named as count for consistent naming with stl functions.
	size_t count(NodeID i) const {
		return mLatpLons.count(i);
	}

	// @brief Insert a latp/lon pair.
	// @param i OSM ID of a node
	// @param coord a latp/lon pair to be inserted
	// @invariant The OSM ID i must be larger than previously inserted OSM IDs of nodes
	//            (though unnecessarily for current impl, future impl may impose that)
	void insert_back(NodeID i, LatpLon coord) {
		mLatpLons.emplace(i, coord);
	}

	// @brief Make the store empty
	void clear() {
		mLatpLons.clear();
	}
};

// way store
typedef vector<NodeID>::const_iterator WayStoreIterator;

class WayStore {
	std::unordered_map<WayID, const vector<NodeID>> mNodeLists;

public:
	// @brief Lookup a node list
	// @param i OSM ID of a way
	// @return A node list
	// @exception NotFound
	NodeList<WayStoreIterator> at(WayID i) const {
		const auto &way = mNodeLists.at(i);
		return { way.cbegin(), way.cend() };
	}

	// @brief Return whether a node list is on the store.
	// @param i Any possible OSM ID
	// @return 1 if found, 0 otherwise
	// @note This function is named as count for consistent naming with stl functions.
	size_t count(WayID i) const {
		return mNodeLists.count(i);
	}

	// @brief Insert a node list.
	// @param i OSM ID of a way
	// @param nodeVec a node vector to be inserted
	// @invariant The OSM ID i must be larger than previously inserted OSM IDs of ways
	//            (though unnecessarily for current impl, future impl may impose that)
	void insert_back(int i, const NodeVec &nodeVec) {
		mNodeLists.emplace(i, nodeVec);
	}

	// @brief Make the store empty
	void clear() {
		mNodeLists.clear();
	}
};

// relation store
typedef vector<WayID>::const_iterator RelationStoreIterator;

class RelationStore {
	std::unordered_map<WayID, const vector<WayID>> mWayLists;

public:
	// @brief Lookup a way list
	// @param i Pseudo OSM ID of a relational way
	// @return A way list
	// @exception NotFound
	WayList<RelationStoreIterator> at(WayID i) const {
		const auto &wayList = mWayLists.at(i);
		return { wayList.cbegin(), wayList.cend() };
	}

	// @brief Return whether a way list is on the store.
	// @param i Any possible OSM ID
	// @return 1 if found, 0 otherwise
	// @note This function is named as count for consistent naming with stl functions.
	size_t count(WayID i) const {
		return mWayLists.count(i);
	}

	// @brief Insert a way list.
	// @param i Pseudo OSM ID of a relation
	// @param outerWayVec A outer way vector to be inserted
	// @param innerWayVec A inner way vector to be inserted
	// @invariant The OSM ID i must be larger than previously inserted OSM IDs of relations
	//            (though unnecessarily for current impl, future impl may impose that)
	void insert_back(WayID i, const WayVec &wayVec) {
		mWayLists.emplace(i, wayVec);
	}

	// @brief Make the store empty
	void clear() {
		mWayLists.clear();
	}
};

//
// OSM store, containing all above.
//
struct OSMStore {
	NodeStore nodes;
	WayStore ways;
	RelationStore relations;

	// Relation -> MultiPolygon
	template<class WayIt>
	MultiPolygon wayListMultiPolygon(WayList<WayIt> wayList) const {
		MultiPolygon mp;
		if (wayList.begin != wayList.end) {
			auto it = wayList.begin;
			bool isOuter = true;
			do {
				Ring ring;
				isOuter = isOuter || *it++ == PSEUDO_WAY_OUTER_MARK;
				for ( ; it != wayList.end && *it != PSEUDO_WAY_OUTER_MARK && *it != PSEUDO_WAY_INNER_MARK; ++it) {
					bool reverse = false;
					if (*it == PSEUDO_WAY_REVERSE_MARK) {
						reverse = true;
						++it;
					}
					fillPoints(ring, ways.at(*it), reverse);
				}
				if (isOuter) {
					Polygon poly;
					poly.outer() = move(ring);
					mp.emplace_back(move(poly));
				} else {
					mp.back().inners().emplace_back(move(ring));
				}
				isOuter = false;
			} while (it != wayList.end);
			// fix winding and force to be closed
			geom::correct(mp);
		}
		return mp;
	}

	MultiPolygon wayListMultiPolygon(WayID relId) const {
		return wayListMultiPolygon(relations.at(relId));
	}

	MultiPolygon wayListMultiPolygon(const WayVec &wayVec) const {
		return wayListMultiPolygon(makeWayList(wayVec));
	}

	// Way -> Polygon
	template<class NodeIt>
	Polygon nodeListPolygon(NodeList<NodeIt> nodeList) const {
		Polygon poly;
		fillPoints(poly.outer(), nodeList);
		geom::correct(poly);
		return poly;
	}

	Polygon nodeListPolygon(WayID wayId) const {
		return nodeListPolygon(ways.at(wayId));
	}

	Polygon nodeListPolygon(const NodeVec &nodeVec) const {
		return nodeListPolygon(makeNodeList(nodeVec));
	}

	// Way -> Linestring
	template<class NodeIt>
	Linestring nodeListLinestring(NodeList<NodeIt> nodeList) const {
		Linestring ls;
		fillPoints(ls, nodeList);
		return ls;
	}

	Linestring nodeListLinestring(WayID wayId) const {
		return nodeListLinestring(ways.at(wayId));
	}

	Linestring nodeListLinestring(const NodeVec &nodeVec) const {
		return nodeListLinestring(makeNodeList(nodeVec));
	}

	WayVec correctMultiPolygonRelation(const WayVec &outerWayVec, const WayVec &innerWayVec) {
		string reason;
		// ways that constitute outer and inner rings
		vector<WayVec> outerWayVecVec, innerWayVecVec;
		// outer and inner rings
		vector<Ring> outerRingVec, innerRingVec;

		// fill {outer/inner}WayVecVec and {outer/inner}RingVec,
		// by correctly connecting ways in {outer/inner}WayVec and grouping the ways into rings.
		for (bool isOuter : {true, false}) {
			// switch inner or outer
			const WayVec &wayVec = isOuter ? outerWayVec : innerWayVec;
			vector<WayVec> &wayVecVec = isOuter ? outerWayVecVec : innerWayVecVec;
			vector<Ring> &ringVec = isOuter ? outerRingVec : innerRingVec;

			vector<bool> matched(wayVec.size(), false);
			vector<pair<LatpLon,LatpLon>> endCoordsVec(wayVec.size());

			// remember begin/end coords of each way
			for (size_t i = 0; i < wayVec.size(); i++) {
				// ignore unavailable ways
				if (!ways.count(wayVec[i])) {
					cerr << "WARNING: correctRelation(): the nodelist of a way in a relation is unavailable.";
					cerr << " Way: " << wayVec[i] << ".";
					cerr << endl;
					matched[i] = true;
					continue;
				}
				auto nodeList = ways.at(wayVec[i]);
				// ignore empty ways
				if (nodeList.begin == nodeList.end) {
					matched[i] = true;
					continue;
				}

				auto itFront = nodeList.begin, itBack = nodeList.begin;
				while (nodeList.begin != nodeList.end) {
					itBack = nodeList.begin++;
				}
				endCoordsVec[i] = make_pair(nodes.at(*itFront), nodes.at(*itBack));
			}

			// construct rings
			for (size_t startIdx = 0; startIdx < wayVec.size(); startIdx++) {
				// the way is already used
				if (matched[startIdx]) continue;

				// search a connected way repeatedly, until the ways make a loop
				WayVec currentWayVec;
				LatpLon startCoord = endCoordsVec[startIdx].first;
				size_t nextIdx = startIdx;
				bool reverse = false;
				do {
					matched[nextIdx] = true;
					if (reverse) {
						currentWayVec.emplace_back(PSEUDO_WAY_REVERSE_MARK);
					}
					currentWayVec.emplace_back(wayVec[nextIdx]);
					LatpLon currentCoord = reverse ? endCoordsVec[nextIdx].first : endCoordsVec[nextIdx].second;

					int64_t minSqd = sqDist(currentCoord, startCoord);
					nextIdx = startIdx;
					for (size_t i = 0; i < wayVec.size(); i++) {
						if (matched[i]) continue;

						for (bool isFirst : {true, false}) {
							LatpLon targetCoord = isFirst ? endCoordsVec[i].first : endCoordsVec[i].second;
							int64_t sqd = sqDist(currentCoord, targetCoord);
							if (sqd < minSqd) {
								minSqd = sqd;
								nextIdx = i;
								reverse = !isFirst;
							} else if (sqd == 0) { // minSqd is already 0
								cerr << "WARNING: correctRelation(): more than two ways share an endpoint.";
								cerr << " Coord: latp=" << currentCoord.latp << ", lon=" << currentCoord.lon << ".";
								cerr << " Way: " << wayVec[i] << ".";
								cerr << " Endpoint: " << (isFirst ? "first" : "second") << ".";
							}
						}
					}

					// a connected way cannot be found, so the nearest way is used
					if (minSqd > 0) {
						cerr << "WARNING: correctRelation(): cannot find a connected way.";
						cerr << " Coord: latp=" << currentCoord.latp << ", lon=" << currentCoord.lon << ".";
						cerr << " Choosen way: " << wayVec[nextIdx] << ".";
						cerr << " Square distance: " << minSqd << ".";
						cerr << endl;
					}
				} while (nextIdx != startIdx);

				// create a ring, for a loop consisting of ways
				Ring currentRing;
				for (auto it = currentWayVec.cbegin(); it != currentWayVec.cend(); ++it) {
					bool reverse = false;
					if (*it == PSEUDO_WAY_REVERSE_MARK) {
						++it;
						reverse = true;
					}
					auto nodeList = ways.at(*it);
					fillPoints(currentRing, nodeList, reverse);
				}
				// fix winding and force to be closed
				geom::correct(currentRing);

				// ring is valid?
				if (!geom::is_valid(currentRing, reason)) {
					cerr << "WARNING: correctRelation(): invalid " << (isOuter ? "outer" : "inner") << " ring.";
					cerr << " Ways:";
					for (auto it = currentWayVec.cbegin(); it != currentWayVec.cend(); ++it) {
						cerr << " ";
						if (*it == PSEUDO_WAY_REVERSE_MARK) {
							++it;
							cerr << "rev";
						}
						cerr << *it;
					}
					cerr << ".";
					cerr << " Reason: " << reason;
					cerr << endl;
					continue;
				}

				// add the ways and the ring to the vectors
				wayVecVec.emplace_back(move(currentWayVec));
				ringVec.emplace_back(move(currentRing));
			}
		}

		// For each outer ring, the ways that constitute the inner rings is collected.
		// PSEUDO_WAY_INNER_MARK separates each inner ring.
		vector<WayVec> innerWayVecForOuter(outerWayVecVec.size());
		// search the nearest outer ring for each inner ring,
		for (size_t k = 0; k < innerRingVec.size(); k++) {
			// search the nearest outer ring (indexed by `parent`)
			size_t parent = SIZE_MAX;
			for (size_t j = 0; j < outerRingVec.size(); j++) {
				if (geom::within(innerRingVec[k], outerRingVec[j])) {
					if (parent == SIZE_MAX || geom::within(outerRingVec[j], outerRingVec[parent])) {
						parent = j;
					}
				}
			}

			// found?
			if (parent == SIZE_MAX) {
				cerr << "WARNING: correctRelation(): an inner ring is not in any outer ring.";
				cerr << " Ways:";
				for (auto it = innerWayVecVec[k].begin(); it != innerWayVecVec[k].end(); ++it) {
					cerr << " ";
					if (*it == PSEUDO_WAY_REVERSE_MARK) {
						++it;
						cerr << "rev";
					}
					cerr << *it;
				}
				cerr << ".";
				cerr << endl;
				continue;
			}

			// add
			innerWayVecForOuter[parent].emplace_back(PSEUDO_WAY_INNER_MARK);
			innerWayVecForOuter[parent].insert(innerWayVecForOuter[parent].end(),
					innerWayVecVec[k].begin(), innerWayVecVec[k].end());
		}

		// convert to the result, by concatenating outer rings by PSEUDO_WAY_OUTER_MARK.
		WayVec result;
		for (size_t j = 0; j < outerWayVecVec.size(); j++) {
			if (j > 0) {
				result.emplace_back(PSEUDO_WAY_OUTER_MARK);
			}
			result.insert(result.end(),
					outerWayVecVec[j].begin(), outerWayVecVec[j].end());
			result.insert(result.end(),
					innerWayVecForOuter[j].begin(), innerWayVecForOuter[j].end());
		}

		// check validity
		if (!geom::is_valid(wayListMultiPolygon(result), reason)) {
			cerr << "WARNING: correctRelation(): invalid multipolygon.";
			if (outerWayVec.empty()) {
				cerr << " Whats?";
			} else {
				cerr << " 1st outer way: " << outerWayVec[0] << ".";
			}
			cerr << " Reason: " << reason;
			cerr << endl;
		}

		return result;
	}

private:
	// helper
	template<class PointRange, class NodeIt>
	void fillPoints(PointRange &points, NodeList<NodeIt> nodeList, bool reverse = false) const {
		Point lastP = Point(123456789.0, 123456789.0); // dummy
		if (!boost::empty(points)) {
			lastP = *(boost::end(points)-1);
		}
		size_t nodeCnt = 0;
		for (auto it = nodeList.begin; it != nodeList.end; ++it) {
			LatpLon ll = nodes.at(*it);
			Point p = geom::make<Point>(ll.lon/10000000.0, ll.latp/10000000.0);
			if (!geom::equals(p, lastP)) {
				nodeCnt++;
				geom::range::push_back(points, p);
			}
			lastP = p;
		}
		auto frontIt = boost::end(points)-nodeCnt;
		auto backIt = boost::end(points)-1;
		if (reverse) {
			while (frontIt < backIt) {
				iter_swap(frontIt++, backIt--);
			}
		}
	}
};
