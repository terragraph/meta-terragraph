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
        Flexibly adopt and install equipment from Terragraph partners
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

const MEMBERS = [
  {
    name: 'Meta',
    imageUrl: 'web/partner-meta.svg',
    href: 'https://www.meta.com/',
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
];

function Member({name, imageUrl, darkStyles, onClick}) {
  const {colorMode} = useColorMode();
  const imgUrl = useBaseUrl(imageUrl);
  return (
    <div className={clsx('col col--4', styles.member)}>
      <img
        className={styles.memberImage}
        style={colorMode === 'dark' && darkStyles ? darkStyles : null}
        src={imgUrl}
        alt={name}
        onClick={onClick}
      />
    </div>
  );
}

function MemberModal({name, imageUrl, href, products}) {
  const imgUrl = useBaseUrl(imageUrl);
  return (
    <div className={styles.modalContainer}>
      <div className={styles.member}>
        <img className={styles.memberModalImage} src={imgUrl} alt={name} />
      </div>
      <hr />
      <div className={styles.modalContents}>
        <div>
          <h3>Links</h3>
          <ul>
            <li>
              <Link to={href}>Website</Link>
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

const MEDIA = [
  {
    // Terragraph
    href: 'https://www.facebook.com/plugins/video.php?href=https%3A%2F%2Fwww.facebook.com%2FEngineering%2Fvideos%2F1966309200184455%2F&show_text=false',
    cls: 'col--12',
  },
  {
    // Why We Build: Connectivity in New York City
    href: 'https://www.youtube-nocookie.com/embed/SF6Wi_MVZ88',
    cls: 'col--6',
  },
  {
    // Why We Build: Meta Connectivity in Alaska
    href: 'https://www.youtube-nocookie.com/embed/9bscnQRJ3K4',
    cls: 'col--6',
  },
];

function Media({href, cls}) {
  return (
    <div className={clsx('col text--center', cls)}>
      <iframe
        className={styles.media}
        src={href}
        width="560"
        height="315"
        scrolling="no"
        frameborder="0"
        allow="accelerometer; autoplay; clipboard-write; encrypted-media; gyroscope; picture-in-picture; web-share"
        allowfullscreen
      />
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

  const [isMemberModalOpen, setMemberModalOpen] = React.useState(false);
  const [memberModalData, setMemberModalData] = React.useState(null);
  const openMemberModal = (data) => {
    setMemberModalData(data);
    setMemberModalOpen(true);
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
              To advance efforts in bringing more people online to a faster
              internet, Meta created Terragraph. Terragraph is a wireless
              technology designed to make deploying gigabit connectivity faster
              and more efficient in markets where trenching fiber is difficult
              and cost-prohibitive. This solution can bring fiber-like speeds to
              your network in a matter of weeks&mdash;and at a fraction of the
              cost.
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
        <section className={clsx('background1', styles.homeContainer)}>
          <div>
            <h2>
              Mission
            </h2>
            <p>
              The Terragraph Project&rsquo;s mission is to create a robust and
              sustainable ecosystem capable of connecting more people to a
              faster internet. Today, there is a robust supply-side ecosystem
              that includes a leading silicon supplier, advanced antenna module
              vendors, and several OEMs offering hardware products and related
              services powered by Terragraph. On the demand side, a multitude of
              connectivity service providers worldwide have adopted
              Terragraph-based solutions and are successfully delivering
              connectivity services to their customers using this technology.
            </p>
            <p>
              Over the years, Meta provided extensive R&D to develop a solution
              for the challenging last-mile connectivity problem. By
              contributing technical specifications to standards and working
              with silicon vendors to implement the Terragraph MAC and PHY in
              commercially available chipsets, Meta empowered its OEM partners
              to implement Terragraph-based radio solutions for connectivity
              service providers to adopt around the world. Meta also engaged
              with spectrum regulatory bodies around the globe to make the case
              for de-licensing the 60 GHz band to ensure Terragraph&rsquo;s
              viability as a connectivity solution in more global markets.
            </p>
            <p>
              Terragraph has joined{' '}
              <Link to={siteConfig.customFields.lfConnectivityUrl}>
                Linux Foundation Connectivity
              </Link>, where the entire software stack is available as open
              source software &ndash; including kernel drivers, user space
              components, cloud software, a network management subsystem and
              millimeter wave network planner solution. By making Terragraph
              software freely available to the community, the ecosystem is now
              able to carry the project forward and continue to expand
              people&rsquo;s access to gigabit internet connectivity.
            </p>
          </div>
        </section>
        <section
          className={clsx(
            'background2',
            styles.homeContainer,
            styles.flexContainer,
          )}>
          <div>
            <h2>Community</h2>
            <div className={clsx('row', styles.flexCenter)}>
              {MEMBERS.map((o) => (
                <Member
                  key={o.name}
                  name={o.name}
                  imageUrl={o.imageUrl}
                  darkStyles={o.darkStyles}
                  onClick={() => openMemberModal(o)}
                />
              ))}
            </div>
          </div>
        </section>
        <section className={clsx('background1', styles.homeContainer)}>
          <div>
            <h2>
              Media
            </h2>
            <div className={clsx('row', styles.flexCenter)}>
              {MEDIA.map((o, idx) => (
                <Media key={idx} href={o.href} cls={o.cls} />
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
        show={isMemberModalOpen}
        onHide={() => setMemberModalOpen(false)}
        className={clsx('shadow--tl', styles.modal)}
        transition={Fade}
        backdropTransition={Fade}
        renderBackdrop={renderBackdrop}
        aria-label="Details">
        {memberModalData && <MemberModal {...memberModalData} />}
      </Modal>
    </Layout>
  );
}
