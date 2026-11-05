/*
 * Copyright 2026 bong-water-water-bong
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
import React from 'react';

/**
 * The 1bit engine mark: the site's 2x2 relay cubes (site/assets/favicon.svg), one cube lit
 * brand-blue at a time, on the card's white logo plate. Inlined rather than fetched because the
 * appliance has no network and GUI3 downloads the other engines' logos from raw.githubusercontent;
 * the wordmark beside it is the card's own label ("1bit engine"), so it stays real text.
 */
export const OnebitEngineLogo: React.FC = () => (
  <svg
    className="onebit-engine-logo"
    viewBox="0 0 24 24"
    role="img"
    aria-label="1bit engine"
    focusable="false"
    data-backend-logo-mark
    style={{ height: 32, width: 32, flex: 'none' }}
  >
    <style>{`
      .onebit-engine-logo .cube {
        fill: #010202;
        stroke: #0230c0;
        stroke-opacity: 0.55;
        animation: onebit-relay 8s ease-in-out infinite;
      }
      .onebit-engine-logo .cube--1 { animation-delay: 0s; }
      .onebit-engine-logo .cube--2 { animation-delay: 2s; }
      .onebit-engine-logo .cube--3 { animation-delay: 4s; }
      .onebit-engine-logo .cube--4 { animation-delay: 6s; }
      @keyframes onebit-relay {
        0%   { fill: #010202; }
        6%   { fill: #0230c0; }
        22%  { fill: #0230c0; }
        28%  { fill: #010202; }
        100% { fill: #010202; }
      }
      @media (prefers-reduced-motion: reduce) {
        .onebit-engine-logo .cube { animation: none; }
        .onebit-engine-logo .cube--1 { fill: #0230c0; }
      }
    `}</style>
    <rect className="cube cube--1" x="2" y="2" width="9" height="9" rx="2" />
    <rect className="cube cube--2" x="13" y="2" width="9" height="9" rx="2" />
    <rect className="cube cube--3" x="2" y="13" width="9" height="9" rx="2" />
    <rect className="cube cube--4" x="13" y="13" width="9" height="9" rx="2" />
  </svg>
);

export default OnebitEngineLogo;
