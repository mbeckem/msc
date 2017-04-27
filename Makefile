MAKEFLAGS := $(MAKEFLAGS) --no-print-directory

EVAL_NODE_BUILDING := \
	results/node_building_random_walk.txt \
	results/node_building_random_walk.json \
	results/node_building_others.txt \
	results/node_building_others.json

EVAL_TREE_BUILDING := results/tree_building.txt results/tree_building.json
EVAL_LARGE_DATASET := results/large_dataset.txt results/large_dataset.json
EVAL_QUERIES := \
	results/queries_geolife.txt results/queries_geolife.json \
	results/queries_osm.txt results/queries_osm.json

EVAL_CHEAP_QUICKLOAD := results/cheap_quickload.txt results/cheap_quickload.json

TREE_VARIANTS := output/variants

QUICKLOAD_PROFILE_GEOLIFE_16M := results/quickload-geolife-16.txt
QUICKLOAD_PROFILE_GEOLIFE_256M := results/quickload-geolife-256.txt
QUICKLOAD_PROFILE_OSM_16M := results/quickload-osm-16.txt
QUICKLOAD_PROFILE_OSM_256M := results/quickload-osm-256.txt

DATASET_STRINGS := \
	output/geolife.strings.txt \
	output/geolife.strings.json \
	output/osm.strings.txt \
	output/osm.strings.json

EXAMPLE_LEAVES := results/hilbert_leaves.pdf results/str_leaves.pdf
STR_LEAVES := results/str_leaves.pdf
IRWI_EXAMPLE_BOXES := results/irwi_example_boxes.pdf

RESULTS := \
	$(EVAL_NODE_BUILDING) \
	$(EVAL_TREE_BUILDING) \
	$(EVAL_LARGE_DATASET) \
	$(EVAL_QUERIES) 	 \
	$(EVAL_CHEAP_QUICKLOAD) \
	$(TREE_VARIANTS) \
	$(QUICKLOAD_PROFILE_GEOLIFE_16M) \
	$(QUICKLOAD_PROFILE_GEOLIFE_256M) \
	$(QUICKLOAD_PROFILE_OSM_16M) \
	$(QUICKLOAD_PROFILE_OSM_256M) \
	$(HILBERT_CURVE) \
	$(EXAMPLE_LEAVES) \
	$(IRWI_EXAMPLE_BOXES) \
	$(DATASET_STRINGS)


all: $(RESULTS)

# Generate a rule that produces multiple outputs.
# Args: 1. The list of output files.
#		2. The command to produce the output files.
#		3. A unique filename prefix for an intermediate target.
define multi_target =
$(1): $(3).intermediate.tmp

$(3).intermediate.tmp:
	$(2)
	touch $(3).intermediate.tmp

.INTERMEDIATE: $(3).intermediate.tmp
endef

$(eval $(call multi_target,$(EVAL_NODE_BUILDING),scripts/eval_node_building.py,node-building))

$(eval $(call multi_target,$(EVAL_TREE_BUILDING),scripts/eval_tree_building.py,tree-building))

$(eval $(call multi_target,$(EVAL_LARGE_DATASET),scripts/eval_large_dataset.py,large-dataset))

$(eval $(call multi_target,$(EVAL_QUERIES),scripts/eval_query.py,queries))

$(eval $(call multi_target,$(EVAL_CHEAP_QUICKLOAD),scripts/eval_cheap_quickload.py, cheap-quickload))

$(TREE_VARIANTS):
	scripts/build_tree_variants.py

$(EVAL_QUERIES): $(EVAL_TREE_BUILDING) $(TREE_VARIANTS) $(DATASET_STRINGS)

$(HILBERT_CURVE):
	scripts/hilbert_curve.py

$(IRWI_EXAMPLE_BOXES):
	scripts/irwi_example.py

$(filter %.txt,$(DATASET_STRINGS)): output/%.txt:
	build/strings --input "$<" > "$@"

$(filter %.json,$(DATASET_STRINGS)): output/%.json:
	build/strings --input "$<" --json > "$@"

$(DATASET_STRINGS): | dataset

$(eval $(call multi_target,$(EXAMPLE_LEAVES),scripts/leaves.py,leaves))

# 1: max memory
# 2: entry file
# 3: output file
define quickload_profile =
	scripts/compile.py --debug-stats
	rm -rf tmp/quickload-profile-tree
	ulimit -Sn 64000 && \
	build/loader --algorithm quickload \
		--tree tmp/quickload-profile-tree \
		--max-memory $(1) \
		--entries $(2) > $(3)
endef

$(QUICKLOAD_PROFILE_GEOLIFE_16M):
	$(call quickload_profile,16,data/geolife.entries,$@)

$(QUICKLOAD_PROFILE_GEOLIFE_256M):
	$(call quickload_profile,256,data/geolife.entries,$@)

$(QUICKLOAD_PROFILE_OSM_16M):
	$(call quickload_profile,16,data/osm.entries,$@)

$(QUICKLOAD_PROFILE_OSM_256M):
	$(call quickload_profile,256,data/osm.entries,$@)

# All results depend on the presence of the datasets.
$(RESULTS): | dataset compile-once

dataset: | compile-once

dataset: FORCE
	@scripts/generate_datasets.py

.PHONY: dataset

compile-once: FORCE
	@mkdir -p build
	@cd build && cmake ../code && $(MAKE) -j5

.PHONY: compile-once

# Always out of date.
FORCE:

.PHONY: FORCE

clean:
	rm -rf results/*
	rm -rf output/*
	rm -rf tmp/*
	@echo "Datasets will not be deleted. Delete files in data/ manually instead."

.PHONY: clean
