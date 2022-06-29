# Python VPP Prototyping

If you build Python API support to a node you can use these scripts to learn VPP API
+ debug more.

## Current Scripts:
- `fibvpp.py`: POC of the C library we need to make for openr-fib-vpp
- `vpp_dir.py`: Show version + all methods available to the Python Client
- `vpp_utils.py`: Shared common function - e.g. loading the JSON

## Add Python to Image:

- Apply Patch
```diff
diff --git a/meta-qoriq/recipes-extended/vpp/vpp_19.01-lsdk.bb b/meta-qoriq/recipes-extended/vpp/vpp_19.01-lsdk.bb
index 83ea201b7..7edef46a9 100644
--- a/meta-qoriq/recipes-extended/vpp/vpp_19.01-lsdk.bb
+++ b/meta-qoriq/recipes-extended/vpp/vpp_19.01-lsdk.bb
@@ -37,7 +37,7 @@ PATCHTOOL = "git"
 # Since this is a benign problem, just silence this error and move on.
 INSANE_SKIP_${PN} += "useless-rpaths"

-inherit cmake pkgconfig python3native
+inherit cmake pkgconfig python3native setuptools3

 S = "${WORKDIR}/git/src"

@@ -67,6 +67,29 @@ FILES_${PN} += "${libdir}/vpp_api_test_plugins"
 # Enable extra safety checks in debug builds
 DEBUG_OPTIMIZATION += "-DCLIB_DEBUG"

+do_configure() {
+    cmake_do_configure
+
+    cd ${S}/vpp-api/python
+    distutils3_do_configure
+}
+
+do_compile() {
+    cmake_do_compile
+
+    cd ${S}/vpp-api/python
+    distutils3_do_compile
+}
+
+do_install() {
+    cmake_do_install
+
+    cd ${S}/vpp-api/python
+    distutils3_do_install
+}
+
+RDEPENDS_${PN} = "python3-pycparser"
+
 # Part of development package?
 FILES_${PN}-dev += "${datadir}/vpp"
```

- Build
- scp to node and upgrade to built image
- After boot remont root: `mount -o remount,rw /`
- scp vpp-dev*.rpm RPM + install (for .api.json files in /usr/share/vpp/api)
