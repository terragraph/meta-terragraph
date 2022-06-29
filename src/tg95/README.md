# Terragraph95

Local web UI for Terragraph, used with the `webui` back-end.

## Available Scripts

Use `fb_yarn_wrapper.sh` instead of `yarn` when running on Facebook devservers.

* `yarn install` - Installs all dependencies to the `node_modules` folder.
* `yarn start` - Runs the app in the development mode.
* `yarn build` - Builds the app for production to the `build` folder and deletes
  the source maps.
* `yarn analyze` - Analyzes the bundle size using `source-map-explorer`. This
  requires source maps, so type `run build` instead of `build` when using the
  wrapper script. To output to a file, run `analyze --html output.html`.

This project was bootstrapped with
[Create React App](https://github.com/facebook/create-react-app). Refer to the
documentation for more details.
