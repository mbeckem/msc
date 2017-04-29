MAKEFLAGS := $(MAKEFLAGS) --no-print-directory

EVAL_NODE_BUILDING := \
	results/node_building_random_walk.txt \
	results/node_building_random_walk.json \
	results/node_building_others.txt \
	results/node_building_others.json

EVAL_TREE_BUILDING := results/tree_building.txt results/tree_building.json
EVAL_LARGE_DATASET := results/large_dataset.txt results/large_dataset.json
EVAL_QUERIES := results/queries.json

EVAL_CHEAP_QUICKLOAD := results/cheap_quickload.txt results/cheap_quickload.json

QUICKLOAD_PROFILE_GEOLIFE_16M := results/quickload-geolife-16.txt
QUICKLOAD_PROFILE_GEOLIFE_256M := results/quickload-geolife-256.txt
QUICKLOAD_PROFILE_OSM_16M := results/quickload-osm-16.txt
QUICKLOAD_PROFILE_OSM_256M := results/quickload-osm-256.txt
QUICKLOAD_PROFILES := \
	$(QUICKLOAD_PROFILE_GEOLIFE_16M) \
	$(QUICKLOAD_PROFILE_GEOLIFE_256M) \
	$(QUICKLOAD_PROFILE_OSM_16M) \
	$(QUICKLOAD_PROFILE_OSM_256M)

EXAMPLE_LEAVES := results/hilbert_leaves.pdf results/str_leaves.pdf
IRWI_EXAMPLE_BOXES := results/irwi_example_boxes.pdf

RESULTS := \
	$(HILBERT_CURVE) \
	$(EXAMPLE_LEAVES) \
	$(IRWI_EXAMPLE_BOXES)


# Note: delete any of the result files (e.g. results/tree_building.txt)
# to repeat the associated experiment. 
all:
	@rm -f make.log
	@echo "Note: Step output will be written into make.log"

	@echo "Making sure that the project is compiled ..."
	@$(MAKE) compile >> make.log

	@echo "Generating dataset ..."
	@$(MAKE) dataset >> make.log

	@echo "Creating trees ..."
	@$(MAKE) trees >> make.log

	@echo "Evaluating cheap quickload variant ..."
	@$(MAKE) $(EVAL_CHEAP_QUICKLOAD) >> make.log

	@echo "Evaluating quickload memory scaling ..."
	@$(MAKE) $(EVAL_LARGE_DATASET) >> make.log

	@echo "Creating quickload trace files ..."
	@$(MAKE) $(QUICKLOAD_PROFILES) >> make.log

	@echo "Evaluating scalable node building algorithm ..."
	@$(MAKE) $(EVAL_NODE_BUILDING) >> make.log

	@echo "Running queries ..."
	@$(MAKE) queries >> make.log

	@echo "Creating graphics ..."
	@$(MAKE) graphics >> make.log

.PHONY: all

dataset:
	scripts/generate_datasets.py

.PHONY: dataset

compile:
	mkdir -p build
	cd build && cmake -DCMAKE_BUILD_TYPE=Release ../code && $(MAKE) -j5

.PHONY: compile

trees:
	@$(MAKE) $(EVAL_TREE_BUILDING)
	scripts/build_tree_variants.py

.PHONY: trees

queries:
	# Files for the translation of label string <-> label index.
	build/strings --input "data/geolife.strings" > "output/geolife.strings.txt"
	build/strings --input "data/geolife.strings" --json > "output/geolife.strings.json"
	build/strings --input "data/osm.strings" > "output/osm.strings.txt"
	build/strings --input "data/osm.strings" --json > "output/osm.strings.json"
	$(MAKE) $(EVAL_QUERIES)

.PHONY: queries

graphics: $(RESULTS)

.PHONY: graphics

clean:
	rm -rf build/*
	rm -rf results/*
	rm -rf output/*
	rm -rf tmp/*
	@echo "Datasets will not be deleted. Delete files in data/ manually instead."

.PHONY: clean

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

$(eval $(call multi_target,$(EVAL_CHEAP_QUICKLOAD),scripts/eval_cheap_quickload.py,cheap-quickload))

$(HILBERT_CURVE):
	scripts/hilbert_curve.py

$(IRWI_EXAMPLE_BOXES):
	scripts/irwi_example.py

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
