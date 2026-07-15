.PHONY: verify verify-engine verify-website tune tune-estimate tune-resume tune-inspect tune-verify tune-clean tuning-clean-all tune-match tune-promote-prepare tuning-promotion-inspect tuning-promotion-prepare tuning-promotion-verify tuning-promotion-promote tuning-promotion-rollback engine-release

override TUNING_GENERATED_DIRS := tuning/annotations tuning/builds tuning/candidates tuning/datasets tuning/features tuning/promotion tuning/runs tuning/search tuning/selections tuning/time tuning/validation

TUNE_ARGS = --release-id "$(RELEASE_ID)" $(if $(TUNE_MODE),--mode "$(TUNE_MODE)",) \
	$(if $(STOCKFISH),--stockfish "$(STOCKFISH)",) \
	$(if $(STOCKFISH_NODES),--stockfish-nodes "$(STOCKFISH_NODES)",) \
	$(if $(filter 1,$(TUNE_TIME)),--tune-time,) \
	$(if $(filter 1,$(RUN_MATCH)),--run-match,)

define require_release_id
	@test -n "$(RELEASE_ID)" || (echo "RELEASE_ID is required (example: make $(1) RELEASE_ID=v2)" >&2; exit 2)
endef

tune:
	$(call require_release_id,tune)
	.venv/bin/python tools/tuning/tuning_pipeline.py run $(TUNE_ARGS)

tune-estimate:
	$(call require_release_id,tune-estimate)
	.venv/bin/python tools/tuning/tuning_pipeline.py estimate $(TUNE_ARGS)

tune-resume:
	$(call require_release_id,tune-resume)
	.venv/bin/python tools/tuning/tuning_pipeline.py resume $(TUNE_ARGS)

tune-inspect:
	$(call require_release_id,tune-inspect)
	.venv/bin/python tools/tuning/tuning_pipeline.py inspect $(TUNE_ARGS)

tune-verify:
	$(call require_release_id,tune-verify)
	.venv/bin/python tools/tuning/tuning_pipeline.py verify $(TUNE_ARGS)

tune-match:
	$(call require_release_id,tune-match)
	.venv/bin/python tools/tuning/tuning_pipeline.py resume $(TUNE_ARGS) --run-match

tune-promote-prepare:
	$(call require_release_id,tune-promote-prepare)
	.venv/bin/python tools/tuning/tuning_pipeline.py promote-prepare $(TUNE_ARGS)

tune-clean:
	$(call require_release_id,tune-clean)
	.venv/bin/python tools/tuning/tuning_pipeline.py clean --release-id "$(RELEASE_ID)" $(if $(filter 1,$(CONFIRM)),--confirm,)

tuning-clean-all:
	@test "$(CONFIRM)" = "1" || (echo "CONFIRM=1 is required (this removes all generated tuning artifacts)" >&2; exit 2)
	@tracked="$$(git ls-files -- $(TUNING_GENERATED_DIRS))"; \
	if test -n "$$tracked"; then \
		echo "Refusing cleanup: tracked files exist under generated tuning directories:" >&2; \
		printf '%s\n' "$$tracked" >&2; \
		exit 2; \
	fi; \
	removed=0; \
	for directory in $(TUNING_GENERATED_DIRS); do \
		if test -d "$$directory"; then \
			count="$$(find "$$directory" -mindepth 1 -print | wc -l)"; \
			removed="$$((removed + count))"; \
			find "$$directory" -mindepth 1 -delete; \
		fi; \
	done; \
	echo "Removed $$removed generated entries from: $(TUNING_GENERATED_DIRS)"; \
	echo "Preserved: tuning/pgn tuning/engines tuning/profiles tuning/schema tuning/parameter-registry.json tuning/pipeline-config.json tuning/promotion-policy.json"

verify: verify-engine verify-website

verify-engine:
	@printf '\n==> Engine tests\n'
	$(MAKE) -C engine test

verify-website:
	@printf '\n==> Website lint\n'
	cd website && npm run lint
	@printf '\n==> Website type-check\n'
	cd website && npm run typecheck
	@printf '\n==> WebSocket queue and protocol tests\n'
	cd website && npm run test:server
	@printf '\n==> Website production build\n'
	cd website && npm run build

tuning-promotion-inspect:
	@test -n "$(CANDIDATE)" || (echo "CANDIDATE is required" >&2; exit 2)
	.venv/bin/python tools/tuning/profile_promotion.py inspect --candidate "$(CANDIDATE)"

tuning-promotion-prepare:
	@test -n "$(CANDIDATE)" || (echo "CANDIDATE is required" >&2; exit 2)
	@test -n "$(RELEASE_ID)" || (echo "RELEASE_ID is required" >&2; exit 2)
	.venv/bin/python tools/tuning/profile_promotion.py prepare --candidate "$(CANDIDATE)" --release-id "$(RELEASE_ID)"

tuning-promotion-verify:
	@test -n "$(PROMOTION)" || (echo "PROMOTION is required" >&2; exit 2)
	.venv/bin/python tools/tuning/profile_promotion.py verify --promotion "$(PROMOTION)"

tuning-promotion-promote:
	@test -n "$(PROMOTION)" || (echo "PROMOTION is required" >&2; exit 2)
	@test -n "$(EXPECTED_CANDIDATE_HASH)" || (echo "EXPECTED_CANDIDATE_HASH is required" >&2; exit 2)
	@test -n "$(EXPECTED_STAGED_HASH)" || (echo "EXPECTED_STAGED_HASH is required" >&2; exit 2)
	@test "$(APPROVE)" = "1" || (echo "APPROVE=1 is required" >&2; exit 2)
	.venv/bin/python tools/tuning/profile_promotion.py promote --promotion "$(PROMOTION)" --expected-candidate-hash "$(EXPECTED_CANDIDATE_HASH)" --expected-staged-hash "$(EXPECTED_STAGED_HASH)" --approve

tuning-promotion-rollback:
	@test -n "$(PROMOTION)" || (echo "PROMOTION is required" >&2; exit 2)
	@test "$(APPROVE)" = "1" || (echo "APPROVE=1 is required" >&2; exit 2)
	.venv/bin/python tools/tuning/profile_promotion.py rollback --promotion "$(PROMOTION)" --approve

engine-release:
	@test -n "$(RELEASE_ID)" || (echo "RELEASE_ID is required" >&2; exit 2)
	@test -n "$(PROFILE)" || (echo "PROFILE is required" >&2; exit 2)
	@mkdir -p tuning/promotion/manual-$(RELEASE_ID)/staged
	.venv/bin/python tools/tuning/generate_tuning_header.py --profile "$(PROFILE)" --registry tuning/parameter-registry.json --output tuning/promotion/manual-$(RELEASE_ID)/staged/generated_tuning_values.hpp
	$(MAKE) -C engine release-build RELEASE_ID="$(RELEASE_ID)" PROFILE_HEADER="../tuning/promotion/manual-$(RELEASE_ID)/staged/generated_tuning_values.hpp" OUTPUT="../engine/chess-engine-$(RELEASE_ID)"
