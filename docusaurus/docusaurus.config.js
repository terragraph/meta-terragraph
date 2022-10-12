/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @format
 */
// @ts-check
// Note: type annotations allow type checking and IDEs autocompletion

// Rehype Plugin to rewrite URLs -- in conjunction with `staticDirectories`,
// this allows us to write markdown with relatives paths such that docs render
// images properly in GitHub or in IDE and still work with Docusaurus.
const visit = require('unist-util-visit');
const rehypeRewriteUrlPlugin = (options) => {
  const transformer = async (ast) => {
    visit(ast, 'jsx', (node) => {
      node.value = node.value.replaceAll("../media/", "/");
    });
  };
  return transformer;
};

// Enable math equation support witih KaTeX
const math = require('remark-math');
const katex = require('rehype-katex');

const REPO_BASE_URL = 'https://github.com/terragraph';
const REPO_URL = REPO_BASE_URL + '/meta-terragraph';
const DISCORD_URL = 'https://discord.gg/HQaxCevzus';
const TGNMS_URL = 'https://github.com/terragraph/tgnms';
const TGPLANNER_URL = 'https://github.com/terragraph/terragraph-planner';
const OPENR_URL = 'https://github.com/facebook/openr';

/** @type {import('@docusaurus/types').Config} */
const config = {
  title: 'Terragraph',
  tagline: 'Solving last mile connectivity challenges around the globe with a wireless multi-gigabit technology',
  url: 'https://terragraph.com',
  baseUrl: '/',
  trailingSlash: false,
  onBrokenLinks: 'throw',
  onBrokenMarkdownLinks: 'warn',
  favicon: 'logo/terragraph-logo-favicon-32x32-full-RGB.png',
  organizationName: 'terragraph', // Usually your GitHub org/user name.
  projectName: 'meta-terragraph', // Usually your repo name.
  staticDirectories: ['static', '../docs/media'],
  presets: [
    [
      'classic',
      /** @type {import('@docusaurus/preset-classic').Options} */
      ({
        docs: {
          path: '../docs',
          exclude: ['build/**', 'README.md'],
          sidebarPath: require.resolve('./sidebars.js'),
          // Please change this to your repo.
          editUrl: ({versionDocsDirPath, docPath}) =>
            `${REPO_URL}/edit/main/docs/${versionDocsDirPath}/${docPath}`,
          remarkPlugins: [math],
          rehypePlugins: [katex, rehypeRewriteUrlPlugin]
        },
        blog: false,
        theme: {
          customCss: require.resolve('./src/css/custom.css'),
        },
      }),
    ],
  ],

  themeConfig:
    /** @type {import('@docusaurus/preset-classic').ThemeConfig} */
    ({
      prism: {
        theme: require('prism-react-renderer/themes/github'),
        darkTheme: require('prism-react-renderer/themes/dracula'),
      },
      image: 'logo/terragraph-logo-full-RGB.png',
      algolia: {
        appId: 'TB9T0CGM6Y',
        apiKey: 'c60c33d83f465d9ec428517c9f9f9ead',
        indexName: 'terragraph',
      },
      docs: {
        sidebar: {
          hideable: true,
        },
      },
      navbar: {
        logo: {
          alt: 'Terragraph Logo',
          src: 'logo/terragraph-logo-favicon-32x32-full-RGB.svg',
          srcDark: 'logo/terragraph-logo-favicon-32x32-white-RGB.svg',
        },
        items: [
          {
            to: REPO_URL, // NOTE: avoiding 'href' to hide IconExternalLink
            position: 'right',
            className: 'githubButton navbarIconButton',
            title: 'GitHub',
          },
          {
            to: DISCORD_URL, // NOTE: avoiding 'href' to hide IconExternalLink
            position: 'right',
            className: 'discordButton navbarIconButton',
            title: 'Discord',
          },
          {
            type: 'dropdown',
            label: 'Docs',
            position: 'right',
            items: [
              {
                type: 'doc',
                docId: 'runbook/README',
                label: 'Runbook',
              },
              {
                type: 'doc',
                docId: 'developer/README',
                label: 'Developer Manual',
              },
              {
                type: 'doc',
                docId: 'whitepapers/README',
                label: 'Whitepapers',
              },
            ],
          },
        ],
      },
      footer: {
        style: 'dark',
        links: [
          {
            title: 'Terragraph',
            items: [
              {
                label: 'Overview',
                to: 'docs/runbook/Overview',
              },
              {
                label: 'Meta Connectivity',
                href: 'https://www.facebook.com/connectivity',
              },
            ],
          },
          {
            title: 'Community',
            items: [
              {
                label: 'GitHub',
                href: REPO_BASE_URL,
              },
              {
                label: 'Discord',
                href: DISCORD_URL,
              },
            ],
          },
          {
            title: 'Legal',
            // Please do not remove the privacy and terms, it's a legal requirement.
            items: [
              {
                label: 'Privacy',
                href: 'https://opensource.facebook.com/legal/privacy/',
              },
              {
                label: 'Terms',
                href: 'https://opensource.facebook.com/legal/terms/',
              },
            ],
          },
        ],
        // Please do not remove the credits, help to publicize Docusaurus :)
        copyright: `Copyright \u{00A9} ${new Date().getFullYear()} Meta Platforms, Inc. Built with Docusaurus.`,
      },
    }),

  stylesheets: [
    {
      href: '/katex/katex.min.css',
      type: 'text/css',
    },
  ],

  customFields: {
    repoUrl: REPO_URL,
    discordUrl: DISCORD_URL,
    tgnmsUrl: TGNMS_URL,
    tgPlannerUrl: TGPLANNER_URL,
    openrUrl: OPENR_URL,
  },
};

module.exports = config;
