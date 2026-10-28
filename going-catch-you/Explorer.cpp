#include "Explorer.h"
#include "Settings.h"
#include "Log.h"

#include <windows.h>
#include <math.h>
#include <string.h>
#include <stdio.h>
#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace
{
	// The executable's image, as laid out in 1.9.32.0.8 (verified against the
	// file): the tables are in .rdata, the type descriptors in .data.
	const UInt32 kImageLo = 0x00400000, kImageHi = 0x01D88000;
	const UInt32 kRdataLo = 0x0106B000, kRdataHi = 0x0123B000;
	const UInt32 kDataLo  = 0x0123B000, kDataHi  = 0x01BC2000;

	enum { kMaxQueue = 700 };
	enum { kMaxWords = 60000 };			// words looked at in one exploration, at most
	enum { kMaxUntyped = 140 };			// pointers to things with no class, followed in one exploration
	enum { kMaxPathCandidates = 24 };	// per exploration
	enum { kMaxNewLines = 180 };		// "new class" lines written over the whole session
	enum { kMaxFieldKeys = 120000 };

	bool PlausiblePointer(UInt32 v)
	{
		return v >= 0x00100000 && v < 0xFFFF0000 && !(v & 3);
	}

	bool InImage(UInt32 v) { return v >= kImageLo && v < kImageHi; }

	// --------------------------------------------------------------- names

	// Classes whose objects are followed no further: the game's world, scene
	// graph, animation and script machinery. They are still named.
	bool Leaf(const char * n)
	{
		static const char * const prefixes[] =
		{
			"Ni", "bhk", "hk", "BSFadeNode", "BSGeometry", "BSTriShape", "BSShader", "BSLighting",
			"TES", "BGS", "Extra", "BSScript", "BSAnimationGraph", "BShkb", "hkb", "Scaleform", "GFx",
			"Projectile", "Explosion", "Hazard", "MagicCaster", "ActiveEffect", "BSAudio", "Menu",
			"BSTask", "BSThread",
		};
		for(size_t i = 0; i < sizeof(prefixes) / sizeof(prefixes[0]); ++i)
			if(strncmp(n, prefixes[i], strlen(prefixes[i])) == 0) return true;

		// other actors, by exact name: the NPC points to its target and to the
		// player, and following those would walk into a second NPC
		static const char * const exact[] = { "Actor", "Character", "PlayerCharacter", "Horse" };
		for(size_t i = 0; i < sizeof(exact) / sizeof(exact[0]); ++i)
			if(strcmp(n, exact[i]) == 0) return true;
		return false;
	}

	// Worth recording field by field.
	bool Interesting(const char * n)
	{
		return strstr(n, "Combat") || strstr(n, "Path") || strstr(n, "Movement") || strstr(n, "Goal") ||
			   strstr(n, "Request") || strstr(n, "Package") || strstr(n, "Procedure") || strstr(n, "Target") ||
			   strstr(n, "Steer") || strstr(n, "Speed") || strstr(n, "Approach");
	}

	// ".?AVMovementControllerNPC@@" -> "MovementControllerNPC"
	void Clean(const char * raw, char * out, size_t size)
	{
		strncpy_s(out, size, raw + 4, _TRUNCATE);
		const size_t n = strlen(out);
		if(n >= 2 && out[n - 1] == '@' && out[n - 2] == '@') out[n - 2] = 0;
	}

	// 24 bits, so that a few hundred places do not collide.
	UInt32 Hash24(const char * s)
	{
		UInt32 h = 2166136261u;
		for(; *s; ++s) { h ^= (UInt8)*s; h *= 16777619u; }
		return (h ^ (h >> 24)) & 0xFFFFFF;
	}

	// ------------------------------------------------------------- records

	struct ClassRec
	{
		char	cls[56];
		UInt32	hunter, idle, player;
	};

	struct PathRec
	{
		char	where[64];
		UInt32	stride;
		UInt32	hunter, idle, player;
		UInt32	nodes;			// the longest run seen
		float	lastToPlayer;	// from the most recent hunter sighting
		float	firstToNpc;
	};

	struct FieldRec
	{
		UInt32	val[2][3];		// up to three distinct values per label
		UInt32	cnt[2][3];		// how often each
		UInt32	nvals[2];
		UInt32	seen[2];
		bool	varied[2];
		char	owner[40];
	};

	std::map<std::string, ClassRec>		s_classes;
	std::map<std::string, PathRec>		s_paths;
	std::map<UInt32, FieldRec>			s_fieldsMotion;		// label 0 = moving, 1 = stopped
	std::map<UInt32, FieldRec>			s_fieldsApproach;	// label 0 = approaching, 1 = going elsewhere
	std::map<UInt32, std::string>		s_hashNames;
	UInt32	s_samples[3] = { 0, 0, 0 };
	UInt32	s_newLines = 0;

	void RecordField(std::map<UInt32, FieldRec> & table, UInt32 key, int label, UInt32 value, const char * owner)
	{
		std::map<UInt32, FieldRec>::iterator it = table.find(key);
		if(it == table.end())
		{
			if(table.size() >= kMaxFieldKeys) return;
			FieldRec fresh;
			memset(&fresh, 0, sizeof(fresh));
			strncpy_s(fresh.owner, sizeof(fresh.owner), owner, _TRUNCATE);
			it = table.insert(std::make_pair(key, fresh)).first;
		}
		FieldRec & f = it->second;
		++f.seen[label];
		if(f.varied[label]) return;
		for(UInt32 i = 0; i < f.nvals[label]; ++i)
			if(f.val[label][i] == value) { ++f.cnt[label][i]; return; }
		if(f.nvals[label] >= 3) { f.varied[label] = true; return; }
		f.val[label][f.nvals[label]] = value;
		f.cnt[label][f.nvals[label]] = 1;
		++f.nvals[label];
	}

	// --------------------------------------------------------- the path finder
	//
	// A path is an array of positions. Its layout is not known, so every start
	// offset is tried with every stride a node could plausibly have, and a run of
	// at least three points, each a believable distance from the last and all
	// near the NPC, is reported.

	struct PathCand
	{
		char	where[64];
		UInt32	address;
		UInt32	offset;
		UInt32	stride;
		UInt32	nodes;
		NiPoint3	first, last;
		float	length;
		float	firstToNpc, lastToNpc, firstToPlayer, lastToPlayer;
		NiPoint3	pts[32];
		UInt32	stored;
	};

	bool BelievablePoint(const float * f, const NiPoint3 & npc)
	{
		const float x = f[0], y = f[1], z = f[2];
		if(!(x == x) || !(y == y) || !(z == z)) return false;				// NaN
		if(fabsf(x) > 250000.0f || fabsf(y) > 250000.0f || fabsf(z) > 30000.0f) return false;
		if(fabsf(x) < 1.0f && fabsf(y) < 1.0f) return false;
		if(fabsf(x - npc.x) > 12000.0f || fabsf(y - npc.y) > 12000.0f) return false;
		return true;
	}

	float Flat(const NiPoint3 & a, const NiPoint3 & b)
	{
		const float dx = a.x - b.x, dy = a.y - b.y;
		return sqrtf(dx * dx + dy * dy);
	}

	void FindPaths(UInt32 address, UInt32 bytes, const char * where, const NiPoint3 & npc, const NiPoint3 & player,
				   std::vector<PathCand> & out)
	{
		static const UInt32 strides[] = { 12, 16, 20, 24, 28, 32, 36, 40, 44, 48, 52, 56, 60, 64, 72, 80, 96 };
		if(!Engine::Readable((const void *)address, bytes)) return;

		const UInt8 * base = (const UInt8 *)address;
		for(size_t si = 0; si < sizeof(strides) / sizeof(strides[0]) && out.size() < kMaxPathCandidates; ++si)
		{
			const UInt32 stride = strides[si];
			for(UInt32 start = 0; start + 12 <= bytes && out.size() < kMaxPathCandidates; start += 4)
			{
				if(!BelievablePoint((const float *)(base + start), npc)) continue;

				// follow the run
				UInt32 n = 1;
				float length = 0.0f;
				const float * f0 = (const float *)(base + start);
				NiPoint3 prev(f0[0], f0[1], f0[2]);
				NiPoint3 firstPt = prev, lastPt = prev;
				while(n < 64)
				{
					const UInt32 off = start + n * stride;
					if(off + 12 > bytes) break;
					const float * f = (const float *)(base + off);
					if(!BelievablePoint(f, npc)) break;
					NiPoint3 p(f[0], f[1], f[2]);
					const float step = Flat(p, prev);
					if(step < 3.0f || step > 1500.0f) break;
					length += step;
					prev = p;
					lastPt = p;
					++n;
				}
				if(n < 3) continue;

				// the start of a run is only the start if the point before it is not
				// part of the same run (otherwise the same path is reported many times)
				if(start >= stride)
				{
					const float * pf = (const float *)(base + start - stride);
					if(BelievablePoint(pf, npc))
					{
						const float step = Flat(NiPoint3(pf[0], pf[1], pf[2]), firstPt);
						if(step >= 3.0f && step <= 1500.0f) continue;
					}
				}

				// A run with every second (third...) node of a run already found at the
				// same place is the same path seen at a coarser stride.
				bool coarser = false;
				for(size_t k = 0; k < out.size(); ++k)
					if(out[k].offset == start && strcmp(out[k].where, where) == 0 && out[k].address == address &&
					   out[k].stride < stride && stride % out[k].stride == 0) { coarser = true; break; }
				if(coarser) { start += (n - 1) * stride; continue; }

				PathCand c;
				memset(&c, 0, sizeof(c));
				strncpy_s(c.where, sizeof(c.where), where, _TRUNCATE);
				c.address = address;
				c.offset = start;
				c.stride = stride;
				c.nodes = n;
				c.first = firstPt;
				c.last = lastPt;
				c.length = length;
				c.firstToNpc = Flat(firstPt, npc);
				c.lastToNpc = Flat(lastPt, npc);
				c.firstToPlayer = Flat(firstPt, player);
				c.lastToPlayer = Flat(lastPt, player);
				for(UInt32 i = 0; i < n && i < 32; ++i)
				{
					const float * f = (const float *)(base + start + i * stride);
					c.pts[i] = NiPoint3(f[0], f[1], f[2]);
					++c.stored;
				}
				out.push_back(c);

				start += (n - 1) * stride;		// do not report the middle of the same run again
			}
		}
	}

	// ------------------------------------------------------------ the walker

	struct Node
	{
		UInt32	address;
		int		depth;
		UInt32	size;
		bool	typed;
		char	chain[60];
		char	cls[56];
	};

	void PushNode(std::vector<Node> & q, std::set<UInt32> & vis, UInt32 addr, int depth, UInt32 size,
				  bool typed, const char * chainText, const char * cls)
	{
		if(q.size() >= kMaxQueue) return;
		if(!vis.insert(addr).second) return;
		Node n;
		n.address = addr; n.depth = depth; n.size = size; n.typed = typed;
		strncpy_s(n.chain, sizeof(n.chain), chainText, _TRUNCATE);
		strncpy_s(n.cls, sizeof(n.cls), cls ? cls : "", _TRUNCATE);
		q.push_back(n);
	}
}

namespace Explorer
{
	const char * ClassName(UInt32 obj)
	{
		if(!PlausiblePointer(obj) || !Engine::Readable((const void *)obj, 4)) return NULL;

		const UInt32 vt = *(const UInt32 *)obj;
		if(vt < kRdataLo || vt >= kRdataHi || (vt & 3)) return NULL;
		if(!Engine::Readable((const void *)(vt - 4), 8)) return NULL;

		const UInt32 col = *(const UInt32 *)(vt - 4);
		if(col < kRdataLo || col >= kRdataHi || (col & 3)) return NULL;
		if(!Engine::Readable((const void *)col, 20)) return NULL;

		const UInt32 * c = (const UInt32 *)col;
		if(c[0] != 0) return NULL;

		const UInt32 td = c[3];
		if(td < kDataLo || td >= kDataHi) return NULL;
		if(!Engine::Readable((const void *)(td + 8), 8)) return NULL;

		const char * n = (const char *)(td + 8);
		if(n[0] != '.' || n[1] != '?' || n[2] != 'A') return NULL;
		return n;
	}

	UInt32 Samples(int role) { return (role >= 0 && role <= 2) ? s_samples[role] : 0; }

	void Reset()
	{
		// The statistics stay for the whole session; there is no per-actor state.
	}

	void Explore(Actor * npc, UInt32 id, const Engine::Chain & chain, int role,
				 const NiPoint3 & npcPos, const NiPoint3 & playerPos,
				 int motion, int approach, bool detailed)
	{
		// A dump looks at one moment; it does not count in the statistics.
		const bool record = !detailed;
		if(record) ++s_samples[role];

		std::vector<Node> queue;
		queue.reserve(kMaxQueue);
		std::set<UInt32> visited;
		std::set<std::string> seenHere;
		std::vector<PathCand> candidates;

		const int maxDepth = (int)Settings::exploreDepth;
		UInt32 words = 0, expanded = 0, untyped = 0;

		// The roots. Sizes are SKSE's where it knows them (the actor, 0x19C; the
		// process manager, 0xA0; the middle process runs to about 0x1B8) and a
		// guess where it does not (the high process).
		{
			const char * actorCls = ClassName((UInt32)npc);
			char clean[56];
			if(actorCls) Clean(actorCls, clean, sizeof(clean)); else strcpy_s(clean, sizeof(clean), "Actor");
			// the process and its parts first, so that their children are looked at
			// before the actor's own crowd of pointers uses up the budget
			PushNode(queue, visited, (UInt32)chain.process, 1, 0xA0, false, "a>88", "ActorProcessManager");
			PushNode(queue, visited, (UInt32)chain.middle, 2, 0x1C0, false, "a>88>4", "MiddleProcess");
			void * high = Engine::Link(chain.process, 0x08);
			if(high) PushNode(queue, visited, (UInt32)high, 2, 0x180, false, "a>88>8", "HighProcess");
			PushNode(queue, visited, (UInt32)npc, 0, 0x19C, true, "a", clean);
		}

		if(detailed)
		{
			_MESSAGE("dump: ===== %08X, %s, %.0f units from the player (dz %+.0f) =====",
					 (unsigned int)id, role == kHunter ? "a hunter" : (role == kPlayer ? "the player" : "not in combat"),
					 Flat(npcPos, playerPos), npcPos.z - playerPos.z);
		}

		for(size_t qi = 0; qi < queue.size() && expanded < Settings::exploreMaxObjects && words < kMaxWords; ++qi)
		{
			const Node n = queue[qi];
			if(!Engine::Readable((const void *)n.address, n.size)) continue;
			++expanded;

			const UInt32 * w = (const UInt32 *)n.address;
			const UInt32 count = n.size / 4;
			words += count;

			// What kind of thing this is, once per exploration and place.
			if(record && n.cls[0])
			{
				const std::string key = std::string(n.chain) + "|" + n.cls;
				if(seenHere.insert(key).second)
				{
					std::map<std::string, ClassRec>::iterator it = s_classes.find(key);
					if(it == s_classes.end())
					{
						ClassRec fresh;
						memset(&fresh, 0, sizeof(fresh));
						strncpy_s(fresh.cls, sizeof(fresh.cls), n.cls, _TRUNCATE);
						it = s_classes.insert(std::make_pair(key, fresh)).first;
						if(role == kHunter && s_newLines < kMaxNewLines)
						{
							++s_newLines;
							_MESSAGE("explore: new in a hunter: %-34s %s", n.chain, n.cls);
						}
					}
					if(role == kHunter) ++it->second.hunter;
					else if(role == kPlayer) ++it->second.player;
					else ++it->second.idle;
				}
			}

			// Detailed dump: the object's own first words.
			if(detailed && (n.typed || n.cls[0]))
			{
				char line[400];
				line[0] = 0;
				const UInt32 show = count < 16 ? count : 16;
				for(UInt32 i = 0; i < show; ++i)
				{
					char piece[12];
					sprintf_s(piece, sizeof(piece), "%08X ", (unsigned int)w[i]);
					strcat_s(line, sizeof(line), piece);
				}
				_MESSAGE("dump:   %-30s %-34s @%08X  %s", n.chain, n.cls[0] ? n.cls : "?", (unsigned int)n.address, line);
			}

			// Field-by-field record, for what is worth it and only for hunters.
			if(record && role == kHunter && n.cls[0] && (Interesting(n.cls) ||
			   strcmp(n.cls, "ActorProcessManager") == 0 || strcmp(n.cls, "MiddleProcess") == 0 || strcmp(n.cls, "HighProcess") == 0))
			{
				// By place, not by class alone: an NPC can hold two objects of one class.
				const std::string owner = std::string(n.chain) + " " + n.cls;
				const UInt32 h = Hash24(owner.c_str());
				s_hashNames[h] = owner;
				const UInt32 lim = count < 64 ? count : 64;
				for(UInt32 i = 0; i < lim; ++i)
				{
					const UInt32 key = (h << 8) | i;
					if(motion != kMotionUnknown)		RecordField(s_fieldsMotion, key, motion == kMoving ? 0 : 1, w[i], n.cls);
					if(approach != kApproachUnknown)	RecordField(s_fieldsApproach, key, approach == kApproaching ? 0 : 1, w[i], n.cls);
				}
			}

			// Could this be a path? Anything with a pointer in it that has no class
			// is a candidate for being an array, and so is the object itself.
			if(n.depth >= 1)
				FindPaths(n.address, n.size < 0x300 ? n.size : 0x300, n.chain, npcPos, playerPos, candidates);

			// Children.
			for(UInt32 i = 0; i < count; ++i)
			{
				const UInt32 v = w[i];
				if(!PlausiblePointer(v) || InImage(v)) continue;
				if(!Engine::Readable((const void *)v, 0x20)) continue;
				if(n.depth + 1 > maxDepth) continue;
				if(visited.count(v)) continue;

				char childChain[60];
				sprintf_s(childChain, sizeof(childChain), "%s>%X", n.chain, (unsigned int)(i * 4));

				const char * raw = ClassName(v);
				if(raw)
				{
					char clean[56];
					Clean(raw, clean, sizeof(clean));
					if(Leaf(clean))
					{
						// named, not followed
						const std::string key = std::string(childChain) + "|" + clean;
						if(record && seenHere.insert(key).second)
						{
							std::map<std::string, ClassRec>::iterator it = s_classes.find(key);
							if(it == s_classes.end())
							{
								ClassRec fresh;
								memset(&fresh, 0, sizeof(fresh));
								strncpy_s(fresh.cls, sizeof(fresh.cls), clean, _TRUNCATE);
								it = s_classes.insert(std::make_pair(key, fresh)).first;
							}
							if(role == kHunter) ++it->second.hunter;
							else if(role == kPlayer) ++it->second.player;
							else ++it->second.idle;
						}
						continue;
					}
					PushNode(queue, visited, v, n.depth + 1, 0x100, true, childChain, clean);
				}
				else if(untyped < kMaxUntyped)
				{
					++untyped;
					PushNode(queue, visited, v, n.depth + 1, 0x200, false, childChain, "");
				}
			}
		}

		// ------------------------------------------------ path candidates
		for(size_t i = 0; i < candidates.size(); ++i)
		{
			const PathCand & c = candidates[i];
			char key[96];
			sprintf_s(key, sizeof(key), "%s +%X stride %u", c.where, (unsigned int)c.offset, (unsigned int)c.stride);
			std::map<std::string, PathRec>::iterator it = s_paths.find(key);
			if(it == s_paths.end())
			{
				PathRec fresh;
				memset(&fresh, 0, sizeof(fresh));
				strncpy_s(fresh.where, sizeof(fresh.where), c.where, _TRUNCATE);
				fresh.stride = c.stride;
				it = s_paths.insert(std::make_pair(std::string(key), fresh)).first;
			}
			PathRec & r = it->second;
			if(record)
			{
				if(role == kHunter)
				{
					++r.hunter;
					r.lastToPlayer = c.lastToPlayer;
					r.firstToNpc = c.firstToNpc;
				}
				else if(role == kPlayer) ++r.player;
				else ++r.idle;
				if(c.nodes > r.nodes) r.nodes = c.nodes;
			}

			if(detailed || (role == kHunter && r.hunter == 1))
			{
				_MESSAGE("%s: path? %08X %s +%X stride %u: %u nodes, %.0f units long; first %.0f from the NPC and %.0f from the player, "
						 "last %.0f from the NPC and %.0f from the player",
						 detailed ? "dump" : "explore", (unsigned int)id, c.where, (unsigned int)c.offset, (unsigned int)c.stride,
						 (unsigned int)c.nodes, c.length, c.firstToNpc, c.firstToPlayer, c.lastToNpc, c.lastToPlayer);
			}
			if(detailed)
			{
				char line[600];
				line[0] = 0;
				for(UInt32 k = 0; k < c.stored; ++k)
				{
					char piece[40];
					sprintf_s(piece, sizeof(piece), "(%.0f %.0f %.0f) ", c.pts[k].x, c.pts[k].y, c.pts[k].z);
					strcat_s(line, sizeof(line), piece);
					if((k % 6) == 5 || k + 1 == c.stored)
					{
						_MESSAGE("dump:     nodes: %s", line);
						line[0] = 0;
					}
				}
			}
		}

		if(detailed)
			_MESSAGE("dump: ===== end of %08X: %u objects, %u words, %u path candidates =====",
					 (unsigned int)id, (unsigned int)expanded, (unsigned int)words, (unsigned int)candidates.size());
	}

	// ------------------------------------------------------------- summary

	namespace
	{
		struct ClassRow { std::string key; ClassRec rec; float score; };
		bool ClassBetter(const ClassRow & a, const ClassRow & b) { return a.score > b.score; }

		struct FieldRow { UInt32 key; FieldRec rec; UInt32 domA, domB; float fracA, fracB; };

		// The value most of a label's samples had, if it is most of them (80%).
		bool Dominant(const FieldRec & f, int label, UInt32 & value, float & fraction)
		{
			UInt32 best = 0, bestIdx = 0;
			for(UInt32 i = 0; i < f.nvals[label]; ++i)
				if(f.cnt[label][i] > best) { best = f.cnt[label][i]; bestIdx = i; }
			if(!f.seen[label]) return false;
			fraction = (float)best / (float)f.seen[label];
			value = f.val[label][bestIdx];
			return fraction >= 0.8f;
		}

		void WriteFields(const char * title, const char * labelA, const char * labelB, std::map<UInt32, FieldRec> & table)
		{
			std::vector<FieldRow> rows;
			for(std::map<UInt32, FieldRec>::const_iterator it = table.begin(); it != table.end(); ++it)
			{
				const FieldRec & f = it->second;
				if(f.seen[0] < 6 || f.seen[1] < 6) continue;
				if(f.varied[0] || f.varied[1]) continue;

				FieldRow row;
				row.key = it->first; row.rec = f;
				if(!Dominant(f, 0, row.domA, row.fracA) || !Dominant(f, 1, row.domB, row.fracB)) continue;
				if(row.domA == row.domB) continue;
				rows.push_back(row);
			}

			_MESSAGE("explore: %s: %u fields tell %s from %s", title, (unsigned int)rows.size(), labelA, labelB);
			for(size_t i = 0; i < rows.size() && i < 24; ++i)
			{
				const FieldRow & r = rows[i];
				std::map<UInt32, std::string>::const_iterator nm = s_hashNames.find(r.key >> 8);
				_MESSAGE("explore:   %-52s +%03X  %s = %08X (%.0f%% of %u)   %s = %08X (%.0f%% of %u)",
						 nm != s_hashNames.end() ? nm->second.c_str() : "?", (unsigned int)((r.key & 0xFF) * 4),
						 labelA, (unsigned int)r.domA, r.fracA * 100.0f, (unsigned int)r.rec.seen[0],
						 labelB, (unsigned int)r.domB, r.fracB * 100.0f, (unsigned int)r.rec.seen[1]);
			}
		}
	}

	void Summary(UInt32 playerPointer)
	{
		(void)playerPointer;
		const UInt32 hs = s_samples[kHunter], is = s_samples[kIdle];
		_MESSAGE("explore: summary: %u hunters, %u NPCs not in combat and %u player samples; %u class places, %u path places",
				 (unsigned int)hs, (unsigned int)is, (unsigned int)s_samples[kPlayer],
				 (unsigned int)s_classes.size(), (unsigned int)s_paths.size());

		// objects a hunter has and an NPC out of combat does not
		std::vector<ClassRow> rows;
		for(std::map<std::string, ClassRec>::const_iterator it = s_classes.begin(); it != s_classes.end(); ++it)
		{
			const ClassRec & c = it->second;
			if(hs < 4) break;
			const float inH = (float)c.hunter / (float)hs;
			const float inI = is ? (float)c.idle / (float)is : 0.0f;
			if(inH < 0.6f || inI > 0.25f) continue;
			ClassRow row; row.key = it->first; row.rec = c; row.score = inH - inI;
			rows.push_back(row);
		}
		std::sort(rows.begin(), rows.end(), ClassBetter);
		_MESSAGE("explore: objects found in hunters and not in NPCs out of combat: %u", (unsigned int)rows.size());
		for(size_t i = 0; i < rows.size() && i < 40; ++i)
		{
			const std::string & k = rows[i].key;
			const size_t bar = k.find('|');
			_MESSAGE("explore:   %-34s %-40s hunters %u/%u, idle %u/%u", k.substr(0, bar).c_str(), rows[i].rec.cls,
					 (unsigned int)rows[i].rec.hunter, (unsigned int)hs, (unsigned int)rows[i].rec.idle, (unsigned int)is);
		}

		// where a path seems to live
		std::vector<std::pair<UInt32, std::string> > ps;
		for(std::map<std::string, PathRec>::const_iterator it = s_paths.begin(); it != s_paths.end(); ++it)
			if(it->second.hunter >= 2) ps.push_back(std::make_pair(it->second.hunter, it->first));
		std::sort(ps.begin(), ps.end());
		std::reverse(ps.begin(), ps.end());
		_MESSAGE("explore: places where hunters have runs of positions: %u", (unsigned int)ps.size());
		for(size_t i = 0; i < ps.size() && i < 20; ++i)
		{
			const PathRec & r = s_paths[ps[i].second];
			_MESSAGE("explore:   %-44s hunters %u, idle %u, longest %u nodes; last node %.0f units from the player (latest)",
					 ps[i].second.c_str(), (unsigned int)r.hunter, (unsigned int)r.idle, (unsigned int)r.nodes, r.lastToPlayer);
		}

		WriteFields("movement", "moving", "stopped", s_fieldsMotion);
		WriteFields("approach", "approaching", "going elsewhere", s_fieldsApproach);
	}
}
