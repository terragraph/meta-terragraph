# Terragraph Release Process
See the "Release Conventions" document (`docs/developer/Release_Conventions.md`)
for a full description of the release process.

Steps are as follows:

1. Write the release notes and update `CHANGELOG.md`. This command gives a
   reasonable start (lists commits between tags *RELEASE_MXX_Y* and
   *RELEASE_MZZ*):
```bash
git log --no-merges --cherry-pick --right-only --oneline RELEASE_MXX_Y...RELEASE_MZZ
```

2. Cut the release on commit *YYYY*. This is local and doesn't push anything.
```bash
./utils/release/cut-release.sh YYYY
```

3. Push the release *mXX* after checking that everything looks correct.
```bash
git push --set-upstream origin releases/mXX
git checkout main
git pull --rebase
git push
git push --tags
```

4. Create a GitHub release for the newly-created tag (*RELEASE_MXX*) and publish
   the release notes there.

5. Build the X86 image and then push it to a Docker Registry (with tags
   *RELEASE_MXX* and *latest*).
```bash
REGISTRY="ghcr.io/terragraph"
IMAGE="e2e-controller"
TAG="RELEASE_MXX"
docker import ./build-x86/tmp/deploy/images/tgx86/terragraph-image-x86-tgx86.tar.gz "$REGISTRY/$IMAGE:$TAG"
docker tag "$REGISTRY/$IMAGE:$TAG" "$REGISTRY/$IMAGE:latest"
docker push "$REGISTRY/$IMAGE:$TAG"
docker push "$REGISTRY/$IMAGE:latest"
```
