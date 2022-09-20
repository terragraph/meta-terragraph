/**
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * This source code is licensed under the MIT license found in the
 * LICENSE file in the root directory of this source tree.
 *
 * @format
 */

import React from 'react';
import Modal from 'react-overlays/Modal';
import {Transition} from 'react-transition-group';
import clsx from 'clsx';
import Layout from '@theme/Layout';
import Link from '@docusaurus/Link';
import {useColorMode} from '@docusaurus/theme-common';
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';
import useBaseUrl from '@docusaurus/useBaseUrl';
import styles from './styles.module.css';

const getFeatures = ({tgnmsUrl, tgPlannerUrl, openrUrl}) => [
  {
    title: '60 GHz spectrum',
    imageUrl: 'web/icon-wireless.svg',
    description: (
      <>Deliver multi-gigabit speeds over unlicensed wide frequency bands</>
    ),
  },
  {
    title: 'TDD/TDMA technology',
    imageUrl: 'web/icon-signal-tower.svg',
    description: (
      <>
        Scale your network over denser areas than previously possible with 60
        GHz spectrum
      </>
    ),
  },
  {
    title: 'Open/R-based mesh design',
    imageUrl: 'web/icon-polygon-tool.svg',
    description: (
      <>
        Efficiently distribute capacity and offer carrier-grade availability to
        customers (<Link to={openrUrl}>learn more</Link>)
      </>
    ),
  },
  {
    title: 'Cloud-based NMS',
    imageUrl: 'web/icon-desktop-cloud.svg',
    description: (
      <>
        Remotely configure, upgrade and monitor your network (
        <Link to={tgnmsUrl}>learn more</Link>)
      </>
    ),
  },
  {
    title: 'Advanced network planning',
    imageUrl: 'web/icon-pin-area.svg',
    description: (
      <>
        Streamline and automate network planning and feasibility assessments (
        <Link to={tgPlannerUrl}>learn more</Link>)
      </>
    ),
  },
  {
    title: 'OEM ecosystem',
    imageUrl: 'web/icon-app-groups.svg',
    description: (
      <>
        Flexibly adopt and install equipment from a network of certified
        partners
      </>
    ),
  },
];

function Feature({imageUrl, title, description}) {
  const imgUrl = useBaseUrl(imageUrl);
  return (
    <div className="col col--4 text--center">
      {imgUrl && (
        <img className={styles.featureImage} src={imgUrl} alt={title} />
      )}
      <h4>{title}</h4>
      <p>{description}</p>
    </div>
  );
}

const PARTNERS = [
  {
    name: 'Siklu',
    imageUrl: 'web/partner-siklu.png',
    href: 'https://www.siklu.com/',
    products: [
      {
        name: 'MultiHaul\u2122 TG Node N366',
        href: 'https://go.siklu.com/multihaul-tg-node-n366-datasheet-lp',
      },
      {
        name: 'Multihaul\u2122 TG Terminal Unit T265',
        href: 'https://go.siklu.com/multihaul-tg-terminal-unit-t265-datasheet-lp',
      },
      {
        name: 'Multihaul\u2122 TG LR Terminal Unit T280',
        href: 'https://go.siklu.com/multihaul-tg-terminal-unit-t280-datasheet-lp',
      },
    ],
  },
  {
    name: 'Cambium',
    imageUrl: 'web/partner-cambium.png',
    href: 'https://www.cambiumnetworks.com/',
    products: [
      {
        name: 'cnWave V5000',
        href: 'https://www.cambiumnetworks.com/products/pmp-distribution/60-ghz-cnwave-v5000/',
      },
      {
        name: 'cnWave V3000',
        href: 'https://www.cambiumnetworks.com/products/pmp-distribution/60-ghz-cnwave-v3000/',
      },
      {
        name: 'cnWave V1000',
        href: 'https://www.cambiumnetworks.com/products/pmp-distribution/60-ghz-cnwave-v1000/',
      },
    ],
  },
  {
    name: 'Edgecore',
    imageUrl: 'web/partner-edgecore.png',
    href: 'https://www.edge-core.com/',
    products: [
      {
        name: 'MLTG-360',
        href: 'https://wifi.edge-core.com/assets/Document/Datasheet/MLTG-360_Datasheet.pdf',
      },
      {
        name: 'MLTG-CN',
        href: 'https://wifi.edge-core.com/assets/Document/Datasheet/MLTG-CN_Datasheet.pdf',
      },
    ],
    darkStyles: {filter: 'invert(100%) hue-rotate(200deg) saturate(720%)'},
  },
  {
    name: 'Ubiquiti',
    imageUrl: 'web/partner-ubiquiti.png',
    href: 'https://www.ui.com/',
    products: [
      {
        name: 'airFiber 60 HD',
        href: 'https://store.ui.com/collections/operator-airfiber/products/airfiber-60-hd',
      },
    ],
  },
  {
    name: 'MikroTik',
    imageUrl: 'web/partner-mikrotik.png',
    href: 'https://www.mikrotik.com/',
  },
  {
    name: 'Qualcomm',
    imageUrl: 'web/partner-qualcomm.png',
    href: 'https://www.qualcomm.com/',
    products: [
      {
        name: 'QCA642x, QCA643x',
        href: 'https://www.qualcomm.com/products/application/networking/qca6428',
      },
    ],
  },
  {
    name: 'Capgemini',
    imageUrl: 'web/partner-capgemini.png',
    href: 'https://www.capgemini.com/',
  },
];

function Partner({name, imageUrl, darkStyles, onClick}) {
  const {colorMode} = useColorMode();
  const imgUrl = useBaseUrl(imageUrl);
  return (
    <div className={clsx('col col--4', styles.partner)}>
      <img
        className={styles.partnerImage}
        style={colorMode === 'dark' && darkStyles ? darkStyles : null}
        src={imgUrl}
        alt={name}
        onClick={onClick}
      />
    </div>
  );
}

function PartnerModal({name, imageUrl, href, products}) {
  const imgUrl = useBaseUrl(imageUrl);
  return (
    <div className={styles.modalContainer}>
      <div className={styles.partner}>
        <img className={styles.partnerModalImage} src={imgUrl} alt={name} />
      </div>
      <hr />
      <div className={styles.modalContents}>
        <div>
          <h3>Links</h3>
          <ul>
            <li>
              <Link to={href}>Partner Website</Link>
            </li>
          </ul>
        </div>
        {products && (
          <div>
            <h3>Products</h3>
            <ul>
              {products.map((o) => (
                <li key={o.name}>
                  <Link to={o.href}>{o.name}</Link>
                </li>
              ))}
            </ul>
          </div>
        )}
      </div>
    </div>
  );
}

const FADE_DURATION = 150; // NOTE: must be the same as .fade CSS transition

const fadeStyles = {
  entering: 'show',
  entered: 'show',
};

const Fade = ({children, ...props}) => (
  <Transition {...props} timeout={FADE_DURATION}>
    {(status, innerProps) =>
      React.cloneElement(children, {
        ...innerProps,
        className: `fade ${fadeStyles[status]} ${children.props.className}`,
      })
    }
  </Transition>
);

export default function Home() {
  const context = useDocusaurusContext();
  const {siteConfig = {}} = context;

  const [isPartnerModalOpen, setPartnerModalOpen] = React.useState(false);
  const [partnerModalData, setPartnerModalData] = React.useState(null);
  const openPartnerModal = (data) => {
    setPartnerModalData(data);
    setPartnerModalOpen(true);
  };
  const renderBackdrop = (props) => (
    <div className={styles.modalBackdrop} {...props} />
  );

  return (
    <Layout description={siteConfig.tagline}>
      <header className={clsx('hero', styles.heroBanner)}>
        <div className={styles.heroVideo}>
          <video
            src={useBaseUrl('web/header.mp4')}
            poster={useBaseUrl('web/header.jpg')}
            autoPlay
            muted
            loop
            playsInline
          />
        </div>
        <div className={styles.heroText}>
          <img
            className={styles.heroTitle}
            src={useBaseUrl('logo/terragraph-logo-full-RGB.svg')}
            alt=""
          />
          <div className={styles.buttons}>
            <Link
              className={clsx(
                'button button--primary button--md shadow--md',
                styles.heroButton,
                styles.getStartedButton,
              )}
              to={useBaseUrl('docs/runbook/Overview')}>
              Get Started
            </Link>
            <Link
              className={clsx(
                'button button--secondary button--md shadow--md',
                'githubButton',
                styles.heroButton,
                styles.heroGithubButton,
              )}
              to={siteConfig.customFields.repoUrl}>
              GitHub
            </Link>
          </div>
        </div>
      </header>
      <main>
        <section className={clsx('background1', styles.homeContainer)}>
          <div>
            <h2>
              Deliver reliable, gigabit speeds easily and cost-effectively
            </h2>
            <p>
              Internet service providers (ISPs) are faced with limited options
              to offer reliable gigabit broadband services&mdash;either lay
              costly fiber or install wireless technologies that can be
              unreliable and difficult to scale.
            </p>
            <p>
              To advance our efforts to bring more people online to a faster
              internet, Meta Connectivity created Terragraph. Terragraph is a
              wireless technology designed to make deploying gigabit
              connectivity faster and more efficient in markets where trenching
              fiber is difficult and cost-prohibitive. This solution can bring
              fiber-like speeds to your network in a matter of weeks&mdash;and
              at a fraction of the cost.
            </p>
            <div className={clsx('item', styles.imgContainer)}>
              <picture>
                <source
                  srcSet={useBaseUrl('web/terragraph-use-cases.svg')}
                  media="(min-width: 768px)"
                />
                <img
                  src={useBaseUrl('web/terragraph-use-cases-mobile.svg')}
                  alt=""
                />
              </picture>
            </div>
          </div>
        </section>
        <section
          className={clsx(
            'background2',
            styles.homeContainer,
            styles.flexContainer,
          )}>
          <div>
            <h2>Features</h2>
            <div className={clsx('row', styles.flexCenter)}>
              {getFeatures(siteConfig.customFields).map(
                ({title, imageUrl, description}) => (
                  <Feature
                    key={title}
                    title={title}
                    imageUrl={imageUrl}
                    description={description}
                  />
                ),
              )}
            </div>
          </div>
        </section>
        <section
          className={clsx(
            'background1',
            styles.homeContainer,
            styles.flexContainer,
          )}>
          <div id="home-partners">
            <h2>Partners</h2>
            <div className={clsx('row', styles.flexCenter)}>
              {PARTNERS.map((o) => (
                <Partner
                  key={o.name}
                  name={o.name}
                  imageUrl={o.imageUrl}
                  darkStyles={o.darkStyles}
                  onClick={() => openPartnerModal(o)}
                />
              ))}
            </div>
          </div>
        </section>
        <section className={clsx('background2', styles.homeContainer)}>
          <div>
            <h2>
              Resources
            </h2>
            <ul>
              <li>
                Looking to deploy Terragraph? Check out our{' '}
                <Link to={useBaseUrl('docs/runbook/')}>
                  runbook
                </Link>
                ,{' '}
                <Link to={useBaseUrl('docs/whitepapers/')}>
                  whitepapers
                </Link>
                , and partner websites to learn more.
              </li>
              <li>
                Developers can explore our public{' '}
                <Link to={siteConfig.customFields.repoUrl}>
                  code repository
                </Link>{' '}
                and extensive{' '}
                <Link to={useBaseUrl('docs/developer/')}>
                  developer manual
                </Link>
                .
              </li>
            </ul>
          </div>
        </section>
      </main>
      <Modal
        show={isPartnerModalOpen}
        onHide={() => setPartnerModalOpen(false)}
        className={clsx('shadow--tl', styles.modal)}
        transition={Fade}
        backdropTransition={Fade}
        renderBackdrop={renderBackdrop}
        aria-label="Partner details">
        {partnerModalData && <PartnerModal {...partnerModalData} />}
      </Modal>
    </Layout>
  );
}
