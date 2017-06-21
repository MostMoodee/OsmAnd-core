#ifndef _OSMAND_BINARY_READ_H
#define _OSMAND_BINARY_READ_H

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#if defined(_WIN32)
#include <io.h>
#else
#include <unistd.h>
#endif
#include <stdio.h>
#include <fstream>
#include <map>
#include <string>
#include <stdint.h>
#include "multipolygons.h"
#include "CommonCollections.h"
#include "commonOsmAndCore.h"

#if defined(WIN32)
#define close _close
#endif
#include "renderRules.h"

static const uint MAP_VERSION = 2;


struct MapTreeBounds {
	uint32_t length;
	uint32_t filePointer;
	uint32_t mapDataBlock;
	uint32_t left ;
	uint32_t right ;
	uint32_t top ;
	uint32_t bottom;

	MapTreeBounds() {
	}
};
struct RoutingIndex;
struct RouteSubregion {
	uint32_t length;
	uint32_t filePointer;
	uint32_t mapDataBlock;
	uint32_t left;
	uint32_t right;
	uint32_t top;
	uint32_t bottom;
	std::vector<RouteSubregion> subregions;
	RoutingIndex* routingIndex;

	RouteSubregion(RoutingIndex* ind) : length(0), filePointer(0), mapDataBlock(0), routingIndex(ind){
	}
};


struct MapRoot: MapTreeBounds {
	uint minZoom ;
	uint maxZoom ;
	std::vector<MapTreeBounds> bounds;
};

enum PART_INDEXES {
	MAP_INDEX = 1,
	POI_INDEX,
	ADDRESS_INDEX,
	TRANSPORT_INDEX,
	ROUTING_INDEX,
};

struct BinaryPartIndex {
	uint32_t length;
	int filePointer;
	PART_INDEXES type;
	std::string name;

	BinaryPartIndex(PART_INDEXES tp) : type(tp) {}
};

struct RoutingIndex : BinaryPartIndex {
//	UNORDERED(map)< uint32_t, tag_value > decodingRules;
	vector< tag_value > decodingRules;
	std::vector<RouteSubregion> subregions;
	std::vector<RouteSubregion> basesubregions;
	RoutingIndex() : BinaryPartIndex(ROUTING_INDEX) {
	}

	void initRouteEncodingRule(uint32_t id, std::string tag, std::string val) {
		tag_value pair = tag_value(tag, val);
		// DEFINE hash
		//encodingRules[pair] = id;
		while(decodingRules.size() < id + 1){
			decodingRules.push_back(pair);
		}
		decodingRules[id] = pair;
	}
};

struct RouteDataObject {
	const static int RESTRICTION_SHIFT = 3;
	const static uint64_t RESTRICTION_MASK = 7;
	const static int HEIGHT_UNDEFINED = -80000;

	RoutingIndex* region;
	std::vector<uint32_t> types ;
	std::vector<uint32_t> pointsX ;
	std::vector<uint32_t> pointsY ;
	std::vector<uint64_t> restrictions ;
	std::vector<std::vector<uint32_t> > pointTypes;
	std::vector<std::vector<uint32_t> > pointNameTypes;
	std::vector<std::vector<uint32_t> > pointNameIds;
	std::vector<std::vector<std::string> > pointNames;
	std::vector<double> heightDistanceArray;
	int64_t id;

	UNORDERED(map)<int, std::string > names;
	vector<pair<uint32_t, uint32_t> > namesIds;

	string getName() {
		if(names.size() > 0) {
			return names.begin()->second;
		}
		return "";
	}

	inline int64_t getId() {
		return id;
	}

	int getSize() {
		int s = sizeof(this);
		s += pointsX.capacity()*sizeof(uint32_t);
		s += pointsY.capacity()*sizeof(uint32_t);
		s += types.capacity()*sizeof(uint32_t);
		s += restrictions.capacity()*sizeof(uint64_t);
		std::vector<std::vector<uint32_t> >::iterator t = pointTypes.begin();
		for(;t!=pointTypes.end(); t++) {
			s+= (*t).capacity() * sizeof(uint32_t);
		}
		t = pointNameTypes.begin();
		for(;t!=pointNameTypes.end(); t++) {
			s+= (*t).capacity() * sizeof(uint32_t);
		}
		std::vector<std::vector<std::string> >::iterator ts = pointNames.begin();
		for(;ts!=pointNames.end(); ts++) {
			s+= (*ts).capacity() *10;
		}
		s += namesIds.capacity()*sizeof(pair<uint32_t, uint32_t>);
		s += names.size()*sizeof(pair<int, string>)*10;
		return s;
	}

	inline int getPointsLength() {
		return pointsX.size();
	}

	bool loop(){
		return pointsX[0] == pointsX[pointsX.size() - 1] && pointsY[0] == pointsY[pointsY.size() - 1] ; 
	}

    void insert(int pos, int x31, int y31) {
        pointsX.insert(pointsX.begin() + pos, x31);
        pointsY.insert(pointsY.begin() + pos, y31);
        if (pointTypes.size() > pos) {
            std::vector<uint32_t> types;
            pointTypes.insert(pointTypes.begin() + pos, types);
        }        
    }

 	string getValue(string tag) {
		uint sz = types.size();
		for(uint i = 0; i < sz; i++) {
			tag_value r = region->decodingRules[types[i]];
			if(r.first == tag) {
				return r.second;
			}
		}
		return "";
	}

	std::vector<double> calculateHeightArray() {
 		if(heightDistanceArray.size() > 0) {
 			return heightDistanceArray;
 		}
 		string strStart = getValue("osmand_ele_start");
 		
 		if(strStart == "") {
 			return heightDistanceArray;
 		}
 		string strEnd = getValue("osmand_ele_end");
 		int startHeight = (int) atof(strStart.c_str());
 		int endHeight = startHeight;
 		if(strEnd != "") {
 			endHeight = (int) atof(strEnd.c_str());
 		}
 		
 		heightDistanceArray.resize(2*getPointsLength(), 0); 
 		double plon = 0;
 		double plat = 0;
 		double prevHeight = startHeight;
 		for(uint k = 0; k < getPointsLength(); k++) {
 			double lon = get31LongitudeX(pointsX[k]);
 			double lat = get31LatitudeY(pointsY[k]);
 			if(k > 0) {
 				double dd = getDistance(plat, plon, lat, lon);
 				double height = HEIGHT_UNDEFINED;
 				if(k == getPointsLength() - 1) {
 					height = endHeight;
 				} else {
 					if(pointTypes.size() > k && pointTypes[k].size() > 0) {
 						uint sz = pointTypes[k].size();
						for(uint sti = 0; sti < sz; sti++) {
							tag_value r = region->decodingRules[pointTypes[k][sti]];
							if (r.first == "osmand_ele_asc") {
 								height = (prevHeight + atof(r.second.c_str()));
 								break;
 							} else if (r.first == "osmand_ele_desc") {
 								height = (prevHeight - atof(r.second.c_str()));
 								break;
 							}
						}
 					}
 				}
 				heightDistanceArray[2*k] = dd;
 				heightDistanceArray[2*k+1] = height;
 				if(height != HEIGHT_UNDEFINED) {
 					// interpolate undefined
 					double totalDistance = dd;
 					int startUndefined = k;
 					while(startUndefined - 1 >= 0 && heightDistanceArray[2*(startUndefined - 1)+1] == HEIGHT_UNDEFINED) {
 						startUndefined --;
 						totalDistance += heightDistanceArray[2*(startUndefined)];
 					}
 					if(totalDistance > 0) {
 						double angle = (height - prevHeight) / totalDistance;
 						for(int j = startUndefined; j < k; j++) {
 							heightDistanceArray[2*j+1] =  ((heightDistanceArray[2*j] * angle) + heightDistanceArray[2*j-1]);
 						}
 					}
 					prevHeight = height;
 				}
 				
 			} else {
 				heightDistanceArray[0] = 0;
 				heightDistanceArray[1] = startHeight;
 			}
 			plat = lat;
 			plon = lon;
 		}
 		return heightDistanceArray;
 	}

  
	string getHighway() {
		uint sz = types.size();
		for(uint i=0; i < sz; i++) {
			tag_value r = region->decodingRules[types[i]];
			if(r.first == "highway") {
				return r.second;
			}
		}
		return "";
	}
	
	bool roundabout(){
		uint sz = types.size();
		for(uint i=0; i < sz; i++) {
			tag_value r = region->decodingRules[types[i]];
			if(r.first == "roundabout" || r.second == "roundabout") {
				return true;
			} else if(r.first == "oneway" && r.second != "no" && loop()) {
				return true;
			}
		}
		return false;
	}

	double directionRoute(int startPoint, bool plus){
		// look at comment JAVA
		return directionRoute(startPoint, plus, 5);
	}

	// Gives route direction of EAST degrees from NORTH ]-PI, PI]
	double directionRoute(int startPoint, bool plus, float dist) {
		int x = pointsX[startPoint];
		int y = pointsY[startPoint];
		int nx = startPoint;
		int px = x;
		int py = y;
		double total = 0;
		do {
			if (plus) {
				nx++;
				if (nx >= (int) pointsX.size()) {
					break;
				}
			} else {
				nx--;
				if (nx < 0) {
					break;
				}
			}
			px = pointsX[nx];
			py = pointsY[nx];
			// translate into meters
			total += abs(px - x) * 0.011 + abs(py - y) * 0.01863;
		} while (total < dist);
		return -atan2( (float)x - px, (float) y - py );
	}

	static double parseSpeed(string v, double def) {
		if(v == "none") {
			return 40;// RouteDataObject::NONE_MAX_SPEED;
		} else {
			int i = findFirstNumberEndIndex(v);
			if (i > 0) {
				double f = atof(v.substr(0, i).c_str());
				f /= 3.6; // km/h -> m/s
				if (v.find("mph") != string::npos) {
					f *= 1.6;
				}
				return f;
			}
		}
		return def;
	}

	static double parseLength(string v, double def) {
		double f = 0;
		// 14'10" 14 - inches, 10 feet
		int i = findFirstNumberEndIndex(v);
		if (i > 0) {
			f += atof(v.substr(0, i).c_str());
			string pref = v.substr(i, v.length());
			float add = 0;
			for(int ik = 0; ik < pref.length(); ik++) {
				if((pref[ik] >= '0' && pref[ik] <= '9') || pref[ik] == '.' || pref[ik] == '-') {
					int first = findFirstNumberEndIndex(pref.substr(ik));
					if(first != -1) {
						add = parseLength(pref.substr(ik), 0);
						pref = pref.substr(0, ik);
					}
					break;
				}
			}
			if (pref.find("km") != string::npos) {
				f *= 1000;  
			}
			if (pref.find("in") != string::npos || pref.find("\"") != string::npos) {
				f *= 0.0254; 
			} else if (pref.find("\'") != string::npos  || pref.find("ft") != string::npos 
						|| pref.find("feet") != string::npos ) {
				// foot to meters
				f *= 0.3048;
			} else if (pref.find("cm") != string::npos) {
				f *= 0.01;
			} else if (pref.find("mile") != string::npos) {
				f *= 1609.34f;
			}
			return f + add;
			
		}
		return def;
	}
	
//	static double parseLength(string v, double def) {
//		// 14"10' not supported
//		int i = findFirstNumberEndIndex(v);
//		if (i > 0) {
//			double f = atof(v.substr(0, i).c_str());
//			if (v.find("\"") != string::npos  || v.find("ft") != string::npos) {
//				// foot to meters
//				f *= 0.3048;
//			}
//			return f;
//		}
//		return def;
//	}


	
	static double parseWeightInTon(string v, double def) {
		int i = findFirstNumberEndIndex(v);
		if (i > 0) {
			double f = atof(v.substr(0, i).c_str());
			if (v.find("\"") != string::npos || v.find("lbs") != string::npos) {
				// lbs -> kg -> ton
				f = (f * 0.4535) / 1000.0;
			}
			return f;
		}
		return def;
	}
};



struct MapIndex : BinaryPartIndex {

	std::vector<MapRoot> levels;

	UNORDERED(map)<int, tag_value > decodingRules;
	// DEFINE hash
	//UNORDERED(map)<tag_value, int> encodingRules;

	int nameEncodingType;
	int refEncodingType;
	int coastlineEncodingType;
	int coastlineBrokenEncodingType;
	int landEncodingType;
	int onewayAttribute ;
	int onewayReverseAttribute ;
	UNORDERED(set)< int > positiveLayers;
	UNORDERED(set)< int > negativeLayers;

	MapIndex() : BinaryPartIndex(MAP_INDEX) {
		nameEncodingType = refEncodingType = coastlineBrokenEncodingType = coastlineEncodingType = -1;
		landEncodingType = onewayAttribute = onewayReverseAttribute = -1;
	}

	void finishInitializingTags() {
		int free = decodingRules.size() * 2 + 1;
		coastlineBrokenEncodingType = free++;
		initMapEncodingRule(0, coastlineBrokenEncodingType, "natural", "coastline_broken");
		if (landEncodingType == -1) {
			landEncodingType = free++;
			initMapEncodingRule(0, landEncodingType, "natural", "land");
		}
	}

	void initMapEncodingRule(uint32_t type, uint32_t id, std::string tag, std::string val) {
		tag_value pair = tag_value(tag, val);
		// DEFINE hash
		//encodingRules[pair] = id;
		decodingRules[id] = pair;

		if ("name" == tag) {
			nameEncodingType = id;
		} else if ("natural" == tag && "coastline" == val) {
			coastlineEncodingType = id;
		} else if ("natural" == tag && "land" == val) {
			landEncodingType = id;
		} else if ("oneway" == tag && "yes" == val) {
			onewayAttribute = id;
		} else if ("oneway" == tag && "-1" == val) {
			onewayReverseAttribute = id;
		} else if ("ref" == tag) {
			refEncodingType = id;
		} else if ("layer" == tag) {
			if (val != "" && val != "0") {
				if (val[0] == '-') {
					negativeLayers.insert(id);
				} else {
					positiveLayers.insert(id);
				}
			}
		}
	}
};


struct BinaryMapFile {
	std::string inputName;
	uint32_t version;
	uint64_t dateCreated;
	std::vector<MapIndex> mapIndexes;
	std::vector<RoutingIndex*> routingIndexes;
	std::vector<BinaryPartIndex*> indexes;
	int fd;
	int routefd;
	bool basemap;
	bool external;
	bool roadOnly;
	bool liveMap;

	bool isBasemap(){
		return basemap;
	}

	bool isExternal(){
		return external;
	}

	bool isLiveMap(){
		return liveMap;
	}
	bool isRoadOnly(){
		return roadOnly;
	}


	~BinaryMapFile() {
		close(fd);
		close(routefd);
	}
};

struct ResultPublisher {
	std::vector< MapDataObject*> result;
	UNORDERED(set)<uint64_t > ids;
	
	bool publish(MapDataObject* r) {
		if(r->id > 0 && !ids.insert(r->id).second) {
			return false;
		}
		result.push_back(r);
		return true;
	}

	bool publishOnlyUnique(std::vector<MapDataObject*> r) {
		for(uint i = 0; i < r.size(); i++) {
			if(!publish(r[i])) {
				delete r[i];
			}
		}
		r.clear();
		return true;
	}

	bool publishAll(std::vector<MapDataObject*> r) {
		for(uint i = 0; i < r.size(); i++) {
			result.push_back(r[i]);
		}
		r.clear();
		return true;
	}

	void clear() {
		result.clear();
		ids.clear();
	}

	bool isCancelled() {
		return false;
	}
	virtual ~ResultPublisher() {
		deleteObjects(result);
	}
};

struct SearchQuery {
	RenderingRuleSearchRequest* req;
	int left;
	int right;
	int top;
	int bottom;
	uint zoom;
	ResultPublisher* publisher;

	coordinates cacheCoordinates;
	bool ocean;

	uint numberOfVisitedObjects;
	uint numberOfAcceptedObjects;
	uint numberOfReadSubtrees;
	uint numberOfAcceptedSubtrees;

	SearchQuery(int l, int r, int t, int b, RenderingRuleSearchRequest* req, ResultPublisher* publisher) :
			req(req), left(l), right(r), top(t), bottom(b),publisher(publisher) {
		numberOfAcceptedObjects = numberOfVisitedObjects = 0;
		numberOfAcceptedSubtrees = numberOfReadSubtrees = 0;
		ocean = false;
	}
	SearchQuery(int l, int r, int t, int b) :
				left(l), right(r), top(t), bottom(b) {
	}

	SearchQuery(){

	}

	bool publish(MapDataObject* obj) {
		return publisher->publish(obj);
	}
};

void searchRouteSubregions(SearchQuery* q, std::vector<RouteSubregion>& tempResult, bool basemap);

void searchRouteDataForSubRegion(SearchQuery* q, std::vector<RouteDataObject*>& list, RouteSubregion* sub);

ResultPublisher* searchObjectsForRendering(SearchQuery* q, bool skipDuplicates, std::string msgNothingFound, int& renderedState);

BinaryMapFile* initBinaryMapFile(std::string inputName);

bool initMapFilesFromCache(std::string inputName) ;

bool closeBinaryMapFile(std::string inputName);

#endif
