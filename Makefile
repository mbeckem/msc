EVAL_NODE_BUILDING := results/eval_node_building.txt
EVAL_TREE_BUILDING := results/eval_tree_building.txt
# EVAL_LARGE_DATASET := results/eval_large_dataset.txt

QUICKLOAD_PROFILE_GEOLIFE_16M := results/quickload-geolife-16.txt
QUICKLOAD_PROFILE_GEOLIFE_256M := results/quickload-geolife-256.txt
QUICKLOAD_PROFILE_OSM_16M := results/quickload-osm-16.txt
QUICKLOAD_PROFILE_OSM_256M := results/quickload-osm-256.txt

HILBERT_CURVE := results/hilbert_curves.pdf
HILBERT_LEAVES := results/hilbert_leaves.pdf
STR_LEAVES := results/str_leaves.pdf
IRWI_EXAMPLE_BOXES := results/irwi_example_boxes.pdf

RESULTS := \
	$(EVAL_NODE_BUILDING) \
	$(EVAL_TREE_BUILDING) \
	$(EVAL_LARGE_DATASET) \
	$(QUICKLOAD_PROFILE_GEOLIFE_16M) \
	$(QUICKLOAD_PROFILE_GEOLIFE_256M) \
	$(QUICKLOAD_PROFILE_OSM_16M) \
	$(QUICKLOAD_PROFILE_OSM_256M) \
	$(HILBERT_CURVE) \
	$(HILBERT_LEAVES) \
	$(STR_LEAVES) \
	$(IRWI_EXAMPLE_BOXES)

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

all: $(RESULTS)

$(EVAL_NODE_BUILDING):
	scripts/eval_node_building.py

$(EVAL_TREE_BUILDING):
	scripts/eval_tree_building.py

$(EVAL_LARGE_DATASET):
	scripts/eval_large_dataset.py

$(HILBERT_CURVE):
	scripts/hilbert_curve.py

$(IRWI_EXAMPLE_BOXES):
	scripts/irwi_example.py

$(eval $(call multi_target,$(STR_LEAVES) $(HILBERT_LEAVES),scripts/leaves.py,leaves))

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
$(RESULTS): | dataset

dataset: FORCE
	@scripts/generate_datasets.py

.PHONY: dataset

# Always out of date.
FORCE:

.PHONY: FORCE
