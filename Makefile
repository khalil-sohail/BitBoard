.PHONY: verify verify-engine verify-website

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
