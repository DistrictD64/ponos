import { useState, useEffect } from 'react';

// Source code content (loaded from public files)
const sourceFiles: Record<string, { name: string; lang: string; desc: string }> = {
  'ponos.h': { name: 'ponos.h', lang: 'c', desc: 'Public API header' },
  'ponos.c': { name: 'ponos.c', lang: 'c', desc: 'Core codec implementation' },
  'main.c': { name: 'main.c', lang: 'c', desc: 'CLI interface' },
  'test.c': { name: 'test.c', lang: 'c', desc: 'Test suite & benchmark' },
  'Makefile': { name: 'Makefile', lang: 'make', desc: 'Build system' },
};

function App() {
  const [activeTab, setActiveTab] = useState('overview');
  const [sourceCode, setSourceCode] = useState<Record<string, string>>({});
  const [activeFile, setActiveFile] = useState('ponos.h');
  const [copied, setCopied] = useState(false);

  useEffect(() => {
    // Load source files
    const loadFiles = async () => {
      const loaded: Record<string, string> = {};
      for (const [key, file] of Object.entries(sourceFiles)) {
        try {
          const resp = await fetch(`/ponos/${file.name}`);
          loaded[key] = await resp.text();
        } catch {
          loaded[key] = `// Could not load ${file.name}`;
        }
      }
      setSourceCode(loaded);
    };
    loadFiles();
  }, []);

  const handleCopy = (text: string) => {
    navigator.clipboard.writeText(text);
    setCopied(true);
    setTimeout(() => setCopied(false), 2000);
  };

  const handleDownload = (filename: string, content: string) => {
    const blob = new Blob([content], { type: 'text/plain' });
    const url = URL.createObjectURL(blob);
    const a = document.createElement('a');
    a.href = url;
    a.download = filename;
    a.click();
    URL.revokeObjectURL(url);
  };

  return (
    <div className="min-h-screen bg-[#0a0a0f] text-gray-200 font-mono">
      {/* Hero Section */}
      <header className="relative overflow-hidden border-b border-gray-800">
        <div className="absolute inset-0 bg-gradient-to-br from-purple-900/20 via-transparent to-blue-900/20" />
        <div className="absolute inset-0 opacity-5" style={{
          backgroundImage: `url("data:image/svg+xml,%3Csvg width='60' height='60' viewBox='0 0 60 60' xmlns='http://www.w3.org/2000/svg'%3E%3Cg fill='none' fill-rule='evenodd'%3E%3Cg fill='%239C92AC' fill-opacity='0.4'%3E%3Cpath d='M36 34v-4h-2v4h-4v2h4v4h2v-4h4v-2h-4zm0-30V0h-2v4h-4v2h4v4h2V6h4V4h-4zM6 34v-4H4v4H0v2h4v4h2v-4h4v-2H6zM6 4V0H4v4H0v2h4v4h2V6h4V4H6z'/%3E%3C/g%3E%3C/g%3E%3C/svg%3E")`,
        }} />
        <div className="relative max-w-6xl mx-auto px-6 py-16">
          <div className="flex items-center gap-4 mb-6">
            <div className="w-16 h-16 rounded-xl bg-gradient-to-br from-purple-600 to-blue-600 flex items-center justify-center text-2xl font-bold shadow-lg shadow-purple-900/50">
              Π
            </div>
            <div>
              <h1 className="text-4xl font-bold bg-gradient-to-r from-purple-400 to-blue-400 bg-clip-text text-transparent">
                Ponos
              </h1>
              <p className="text-gray-400 text-sm">Asymmetric Lossless Compression Codec</p>
            </div>
          </div>
          <p className="text-lg text-gray-300 max-w-3xl leading-relaxed">
            A portable, pure C11 lossless compression codec. The compressor does all the heavy work —
            exhaustive search, optimal parsing, adaptive entropy coding. The decompressor is fast,
            simple, and uses minimal memory. <span className="text-purple-400">Hard on the encoder, easy on the decoder.</span>
          </p>
          <div className="flex flex-wrap gap-3 mt-8">
            <span className="px-3 py-1 rounded-full bg-purple-900/40 border border-purple-700/50 text-purple-300 text-xs">
              Pure C11
            </span>
            <span className="px-3 py-1 rounded-full bg-blue-900/40 border border-blue-700/50 text-blue-300 text-xs">
              No Extensions
            </span>
            <span className="px-3 py-1 rounded-full bg-green-900/40 border border-green-700/50 text-green-300 text-xs">
              Portable
            </span>
            <span className="px-3 py-1 rounded-full bg-amber-900/40 border border-amber-700/50 text-amber-300 text-xs">
              Single-Threaded
            </span>
            <span className="px-3 py-1 rounded-full bg-rose-900/40 border border-rose-700/50 text-rose-300 text-xs">
              Lossless
            </span>
          </div>
        </div>
      </header>

      {/* Navigation */}
      <nav className="sticky top-0 z-50 bg-[#0a0a0f]/90 backdrop-blur-sm border-b border-gray-800">
        <div className="max-w-6xl mx-auto px-6">
          <div className="flex gap-1 overflow-x-auto">
            {['overview', 'architecture', 'source', 'format'].map(tab => (
              <button
                key={tab}
                onClick={() => setActiveTab(tab)}
                className={`px-4 py-3 text-sm capitalize transition-colors whitespace-nowrap ${
                  activeTab === tab
                    ? 'text-purple-400 border-b-2 border-purple-400'
                    : 'text-gray-500 hover:text-gray-300'
                }`}
              >
                {tab}
              </button>
            ))}
          </div>
        </div>
      </nav>

      {/* Content */}
      <main className="max-w-6xl mx-auto px-6 py-10">
        {activeTab === 'overview' && <OverviewSection />}
        {activeTab === 'architecture' && <ArchitectureSection />}
        {activeTab === 'source' && (
          <SourceSection
            sourceCode={sourceCode}
            activeFile={activeFile}
            setActiveFile={setActiveFile}
            onCopy={handleCopy}
            onDownload={handleDownload}
            copied={copied}
          />
        )}
        {activeTab === 'format' && <FormatSection />}
      </main>

      {/* Footer */}
      <footer className="border-t border-gray-800 py-8 text-center text-gray-600 text-sm">
        <p>Ponos Codec — Named after the Greek spirit of hard toil and endurance</p>
        <p className="mt-1">Pure C11 • No dependencies • No CPU extensions • Fully portable</p>
      </footer>
    </div>
  );
}

function OverviewSection() {
  const features = [
    {
      icon: 'fa-bolt',
      color: 'purple',
      title: 'Fast Decompression',
      desc: 'Simple decode path: read range-coded symbols, copy matches. No hash tables, no search, no DP.',
    },
    {
      icon: 'fa-brain',
      color: 'blue',
      title: 'Optimal Parsing',
      desc: 'Dynamic programming over each block finds the minimum-cost token sequence. Levels 5-9.',
    },
    {
      icon: 'fa-feather',
      color: 'green',
      title: 'Low Decoder Memory',
      desc: 'Under 3 MiB for 1 MiB blocks. Just models + output buffer + range coder state.',
    },
    {
      icon: 'fa-shield-halved',
      color: 'amber',
      title: 'CRC32 Integrity',
      desc: 'Every block is CRC32-verified. Corruption is detected immediately on decompression.',
    },
    {
      icon: 'fa-microchip',
      color: 'rose',
      title: 'Fully Portable',
      desc: 'Pure C11. No SSE, AVX, NEON, BMI, intrinsics, or inline assembly. Works everywhere.',
    },
    {
      icon: 'fa-layer-group',
      color: 'cyan',
      title: 'Block-Based',
      desc: 'Configurable 64 KiB to 4 MiB blocks. Default 1 MiB. Independent block decompression.',
    },
  ];

  return (
    <div>
      <h2 className="text-2xl font-bold text-white mb-6">Design Philosophy</h2>
      <div className="bg-gray-900/50 border border-gray-800 rounded-xl p-6 mb-8">
        <p className="text-gray-300 leading-relaxed">
          Ponos follows an <strong className="text-purple-400">asymmetric design</strong>: the encoder
          (Ponos — the Greek spirit of hard toil) performs exhaustive, slow work to find the best
          compression. The decoder takes the fast, simple path — just read symbols and copy data.
          This makes Ponos ideal for scenarios where data is compressed once but decompressed many times,
          or where decoder resources are constrained.
        </p>
        <div className="grid grid-cols-1 md:grid-cols-2 gap-4 mt-6">
          <div className="bg-purple-900/20 border border-purple-800/40 rounded-lg p-4">
            <h3 className="text-purple-400 font-bold mb-2">
              <i className="fas fa-gear mr-2" />Encoder (Ponos)
            </h3>
            <ul className="text-sm text-gray-400 space-y-1">
              <li>• LZ77 with 4-byte hash chains</li>
              <li>• Search depth up to 256 (level 9)</li>
              <li>• Optimal parsing via DP (level ≥ 5)</li>
              <li>• Adaptive range coder with contexts</li>
              <li>• May use up to 64 MiB RAM</li>
              <li>• Single-threaded, may be very slow</li>
            </ul>
          </div>
          <div className="bg-blue-900/20 border border-blue-800/40 rounded-lg p-4">
            <h3 className="text-blue-400 font-bold mb-2">
              <i className="fas fa-bolt mr-2" />Decoder (Everyone Else)
            </h3>
            <ul className="text-sm text-gray-400 space-y-1">
              <li>• Read range-coded symbols</li>
              <li>• Decode literal or match</li>
              <li>• Copy from window for matches</li>
              <li>• No hash tables or search</li>
              <li>• Under 3 MiB memory</li>
              <li>• Fast and simple inner loop</li>
            </ul>
          </div>
        </div>
      </div>

      <h2 className="text-2xl font-bold text-white mb-6">Features</h2>
      <div className="grid grid-cols-1 md:grid-cols-2 lg:grid-cols-3 gap-4">
        {features.map((f, i) => (
          <div key={i} className={`bg-gray-900/50 border border-gray-800 rounded-xl p-5 hover:border-${f.color}-800/60 transition-colors`}>
            <div className={`w-10 h-10 rounded-lg bg-${f.color}-900/40 flex items-center justify-center mb-3`}>
              <i className={`fas ${f.icon} text-${f.color}-400`} />
            </div>
            <h3 className="text-white font-bold mb-2">{f.title}</h3>
            <p className="text-sm text-gray-400">{f.desc}</p>
          </div>
        ))}
      </div>

      <h2 className="text-2xl font-bold text-white mt-10 mb-6">Compression Levels</h2>
      <div className="overflow-x-auto">
        <table className="w-full text-sm border-collapse">
          <thead>
            <tr className="border-b border-gray-800">
              <th className="text-left py-2 px-3 text-gray-400">Level</th>
              <th className="text-left py-2 px-3 text-gray-400">Hash Bits</th>
              <th className="text-left py-2 px-3 text-gray-400">Max Depth</th>
              <th className="text-left py-2 px-3 text-gray-400">Parsing</th>
              <th className="text-left py-2 px-3 text-gray-400">Speed</th>
              <th className="text-left py-2 px-3 text-gray-400">Ratio</th>
            </tr>
          </thead>
          <tbody>
            {[
              [1, 14, 4, 'Greedy', '■■■■■■■■■■', '■□□□□□□□□□'],
              [2, 14, 4, 'Greedy+Lazy', '■■■■■■■■□□', '■■□□□□□□□□'],
              [3, 16, 16, 'Greedy+Lazy', '■■■■■■■□□□', '■■■□□□□□□□'],
              [4, 16, 16, 'Greedy+Lazy', '■■■■■■□□□□', '■■■■□□□□□□'],
              [5, 18, 32, 'Optimal', '■■■■■□□□□□', '■■■■■□□□□□'],
              [6, 18, 32, 'Optimal', '■■■■□□□□□□', '■■■■■■□□□□'],
              [7, 20, 128, 'Optimal', '■■■□□□□□□□', '■■■■■■■□□□'],
              [8, 20, 128, 'Optimal', '■■□□□□□□□□', '■■■■■■■■□□'],
              [9, 22, 256, 'Optimal', '■□□□□□□□□□', '■■■■■■■■■□'],
            ].map(([level, hash, depth, parse, speed, ratio], i) => (
              <tr key={i} className="border-b border-gray-800/50 hover:bg-gray-800/30">
                <td className="py-2 px-3 text-purple-400 font-bold">{level as number}</td>
                <td className="py-2 px-3 text-gray-300">2^{hash as number}</td>
                <td className="py-2 px-3 text-gray-300">{depth as number}</td>
                <td className="py-2 px-3 text-gray-300">{parse as string}</td>
                <td className="py-2 px-3 text-green-400 font-mono text-xs">{speed as string}</td>
                <td className="py-2 px-3 text-amber-400 font-mono text-xs">{ratio as string}</td>
              </tr>
            ))}
          </tbody>
        </table>
      </div>
    </div>
  );
}

function ArchitectureSection() {
  return (
    <div>
      <h2 className="text-2xl font-bold text-white mb-6">Codec Architecture</h2>

      {/* Pipeline diagram */}
      <div className="bg-gray-900/50 border border-gray-800 rounded-xl p-6 mb-8 overflow-x-auto">
        <h3 className="text-lg font-bold text-purple-400 mb-4">Encoding Pipeline</h3>
        <div className="flex flex-wrap items-center gap-2 text-sm">
          <Step label="Input Block" color="gray" />
          <Arrow />
          <Step label="Hash Chain\nMatch Finder" color="blue" />
          <Arrow />
          <Step label="Optimal Parser\n(DP)" color="purple" />
          <Arrow />
          <Step label="Token Stream" color="amber" />
          <Arrow />
          <Step label="Range Coder\n(Adaptive)" color="green" />
          <Arrow />
          <Step label="Compressed\nBytes" color="rose" />
        </div>
      </div>

      <div className="bg-gray-900/50 border border-gray-800 rounded-xl p-6 mb-8 overflow-x-auto">
        <h3 className="text-lg font-bold text-blue-400 mb-4">Decoding Pipeline</h3>
        <div className="flex flex-wrap items-center gap-2 text-sm">
          <Step label="Compressed\nBytes" color="rose" />
          <Arrow />
          <Step label="Range Decoder" color="green" />
          <Arrow />
          <Step label="Decode\nis_match" color="amber" />
          <Arrow />
          <Step label="Literal or\nMatch Params" color="purple" />
          <Arrow />
          <Step label="Output\nBlock" color="gray" />
        </div>
      </div>

      {/* Range Coder explanation */}
      <div className="bg-gray-900/50 border border-gray-800 rounded-xl p-6 mb-8">
        <h3 className="text-lg font-bold text-green-400 mb-4">
          <i className="fas fa-calculator mr-2" />Binary Range Coder
        </h3>
        <div className="text-sm text-gray-300 space-y-3">
          <p>
            The entropy coder is a <strong>binary arithmetic range coder</strong> with 12-bit adaptive
            probability models. It maintains an interval [low, low+range) and narrows it for each symbol.
          </p>
          <div className="bg-black/40 rounded-lg p-4 font-mono text-xs overflow-x-auto">
            <div className="text-green-400">// Encoding a bit with probability p0 (prob of 0):</div>
            <div className="text-gray-300">split = (range {'>>'} 12) * p0;</div>
            <div className="text-gray-300">if bit == 0: range = split;</div>
            <div className="text-gray-300">if bit == 1: low += split; range -= split;</div>
            <div className="mt-2 text-green-400">// Normalize when range {'<'} 2^24:</div>
            <div className="text-gray-300">output top byte of low; low {'<<'}= 8; range {'<<'}= 8;</div>
          </div>
          <p>
            <strong>Carry propagation</strong> is handled by using a 64-bit <code className="text-purple-400">low</code> value
            with a cache mechanism. When the top byte is ambiguous (0xFF), it's cached until the
            carry is resolved.
          </p>
          <p>
            <strong>Adaptation:</strong> After each symbol, the probability is updated using exponential
            moving average: <code className="text-purple-400">p += (target - p) {'>>'} 5</code>. This gives
            fast adaptation to changing data characteristics.
          </p>
        </div>
      </div>

      {/* Models */}
      <div className="bg-gray-900/50 border border-gray-800 rounded-xl p-6 mb-8">
        <h3 className="text-lg font-bold text-amber-400 mb-4">
          <i className="fas fa-diagram-project mr-2" />Adaptive Models
        </h3>
        <div className="grid grid-cols-1 md:grid-cols-2 gap-4 text-sm">
          <div className="bg-black/30 rounded-lg p-4">
            <h4 className="text-white font-bold mb-2">is_match [32 contexts]</h4>
            <p className="text-gray-400">
              Context = (prev_byte {'>>'} 5) × 4 + match_state<br />
              Determines if next token is literal or match.
            </p>
          </div>
          <div className="bg-black/30 rounded-lg p-4">
            <h4 className="text-white font-bold mb-2">literal [256 × 255 probs]</h4>
            <p className="text-gray-400">
              Binary tree over 256 values.<br />
              Context = previous decoded byte.<br />
              8 binary decisions per byte.
            </p>
          </div>
          <div className="bg-black/30 rounded-lg p-4">
            <h4 className="text-white font-bold mb-2">length [17 contexts]</h4>
            <p className="text-gray-400">
              Unary prefix for bit-length, then binary bits.<br />
              Short lengths (0-3) encoded directly.
            </p>
          </div>
          <div className="bg-black/30 rounded-lg p-4">
            <h4 className="text-white font-bold mb-2">distance [24 slots + bits]</h4>
            <p className="text-gray-400">
              Slot + extra bits scheme.<br />
              Slots 0-3: direct (dist 1-4).<br />
              Slot k≥4: k-2 extra bits.
            </p>
          </div>
        </div>
      </div>

      {/* LZ77 */}
      <div className="bg-gray-900/50 border border-gray-800 rounded-xl p-6">
        <h3 className="text-lg font-bold text-rose-400 mb-4">
          <i className="fas fa-link mr-2" />LZ77 Match Finder
        </h3>
        <div className="text-sm text-gray-300 space-y-3">
          <p>
            Uses <strong>4-byte hash chains</strong> with a multiplicative hash function (golden ratio constant).
            The hash table maps 4-byte sequences to their most recent position. A chain array links
            previous positions with the same hash.
          </p>
          <div className="bg-black/40 rounded-lg p-4 font-mono text-xs">
            <div className="text-green-400">// Hash function:</div>
            <div className="text-gray-300">hash4(a,b,c,d) = (a | b{'<<'}8 | c{'<<'}16 | d{'<<'}24) × 0x9E3779B9 {'>>'} (32 - HASH_BITS)</div>
            <div className="mt-2 text-green-400">// Chain traversal:</div>
            <div className="text-gray-300">for depth in 0..max_depth:</div>
            <div className="text-gray-300">{'  '}compare data[pos..] with data[chain_pos..]</div>
            <div className="text-gray-300">{'  '}chain_pos = chain[chain_pos]</div>
          </div>
          <p>
            <strong>Lazy matching</strong> (level ≥ 3): Before emitting a match, check if the next position
            has a significantly better match. If so, emit a literal instead.
          </p>
        </div>
      </div>
    </div>
  );
}

function Step({ label, color }: { label: string; color: string }) {
  const colorMap: Record<string, string> = {
    gray: 'border-gray-600 bg-gray-800/80 text-gray-300',
    blue: 'border-blue-600 bg-blue-900/40 text-blue-300',
    purple: 'border-purple-600 bg-purple-900/40 text-purple-300',
    amber: 'border-amber-600 bg-amber-900/40 text-amber-300',
    green: 'border-green-600 bg-green-900/40 text-green-300',
    rose: 'border-rose-600 bg-rose-900/40 text-rose-300',
  };
  return (
    <div className={`px-3 py-2 rounded-lg border ${colorMap[color]} text-center whitespace-pre-line min-w-[80px]`}>
      {label}
    </div>
  );
}

function Arrow() {
  return <span className="text-gray-600 text-lg">→</span>;
}

function SourceSection({
  sourceCode,
  activeFile,
  setActiveFile,
  onCopy,
  onDownload,
  copied,
}: {
  sourceCode: Record<string, string>;
  activeFile: string;
  setActiveFile: (f: string) => void;
  onCopy: (text: string) => void;
  onDownload: (name: string, content: string) => void;
  copied: boolean;
}) {
  const code = sourceCode[activeFile] || 'Loading...';
  const lines = code.split('\n');

  return (
    <div>
      <div className="flex items-center justify-between mb-4">
        <h2 className="text-2xl font-bold text-white">Source Code</h2>
        <div className="flex gap-2">
          <button
            onClick={() => onCopy(code)}
            className="px-3 py-1.5 rounded-lg bg-gray-800 border border-gray-700 text-sm text-gray-300 hover:bg-gray-700 transition-colors"
          >
            <i className={`fas ${copied ? 'fa-check text-green-400' : 'fa-copy'} mr-1`} />
            {copied ? 'Copied!' : 'Copy'}
          </button>
          <button
            onClick={() => onDownload(sourceFiles[activeFile]?.name || 'file', code)}
            className="px-3 py-1.5 rounded-lg bg-purple-900/50 border border-purple-700/50 text-sm text-purple-300 hover:bg-purple-800/50 transition-colors"
          >
            <i className="fas fa-download mr-1" />
            Download
          </button>
        </div>
      </div>

      {/* File tabs */}
      <div className="flex gap-1 overflow-x-auto mb-0 bg-gray-900/80 rounded-t-xl border border-gray-800 border-b-0 p-1">
        {Object.entries(sourceFiles).map(([key, file]) => (
          <button
            key={key}
            onClick={() => setActiveFile(key)}
            className={`px-3 py-2 rounded-lg text-xs whitespace-nowrap transition-colors ${
              activeFile === key
                ? 'bg-gray-800 text-purple-400 border border-purple-700/50'
                : 'text-gray-500 hover:text-gray-300 hover:bg-gray-800/50'
            }`}
          >
            <i className="fas fa-file-code mr-1" />
            {file.name}
          </button>
        ))}
      </div>

      {/* File description */}
      <div className="bg-gray-900/60 border-x border-gray-800 px-4 py-2 text-xs text-gray-500">
        {sourceFiles[activeFile]?.desc} — {lines.length} lines
      </div>

      {/* Code display */}
      <div className="bg-[#0d1117] border border-gray-800 rounded-b-xl overflow-hidden">
        <div className="overflow-x-auto max-h-[600px] overflow-y-auto">
          <pre className="text-xs leading-5 p-0 m-0">
            <code>
              {lines.map((line, i) => (
                <div key={i} className="flex hover:bg-gray-800/30">
                  <span className="select-none text-gray-600 text-right pr-4 pl-4 min-w-[3.5rem] border-r border-gray-800/50">
                    {i + 1}
                  </span>
                  <span className="pl-4 pr-4 whitespace-pre">
                    <SyntaxLine line={line} />
                  </span>
                </div>
              ))}
            </code>
          </pre>
        </div>
      </div>

      {/* Download all */}
      <div className="mt-6 bg-gray-900/50 border border-gray-800 rounded-xl p-6">
        <h3 className="text-lg font-bold text-white mb-3">
          <i className="fas fa-file-zipper mr-2 text-purple-400" />
          Download All Files
        </h3>
        <p className="text-sm text-gray-400 mb-4">
          Download individual files or build the project locally with <code className="text-purple-400">make</code>.
        </p>
        <div className="flex flex-wrap gap-2">
          {Object.entries(sourceFiles).map(([key, file]) => (
            <button
              key={key}
              onClick={() => onDownload(file.name, sourceCode[key] || '')}
              className="px-3 py-2 rounded-lg bg-gray-800 border border-gray-700 text-sm text-gray-300 hover:bg-gray-700 transition-colors"
            >
              <i className="fas fa-download mr-1 text-xs" />
              {file.name}
            </button>
          ))}
        </div>
        <div className="mt-4 bg-black/40 rounded-lg p-3 font-mono text-xs text-gray-400">
          <div className="text-green-400 mb-1"># Build instructions:</div>
          <div>$ cd ponos/</div>
          <div>$ make</div>
          <div>$ ./ponos c input.txt output.pono 5</div>
          <div>$ ./ponos d output.pono restored.txt</div>
          <div className="mt-1 text-green-400"># Run tests:</div>
          <div>$ make test</div>
        </div>
      </div>
    </div>
  );
}

// Simple syntax highlighting
function SyntaxLine({ line }: { line: string }) {
  // Very basic C syntax highlighting using spans
  const highlighted = highlightC(line);
  return <span dangerouslySetInnerHTML={{ __html: highlighted }} />;
}

function highlightC(line: string): string {
  // Escape HTML
  let s = line
    .replace(/&/g, '&amp;')
    .replace(/</g, '&lt;')
    .replace(/>/g, '&gt;');

  // Comments
  s = s.replace(/(\/\/.*$)/gm, '<span class="text-gray-500">$1</span>');
  s = s.replace(/(\/\*.*?\*\/)/g, '<span class="text-gray-500">$1</span>');

  // Strings
  s = s.replace(/(&quot;[^&]*?&quot;|"[^"]*?")/g, '<span class="text-green-400">$1</span>');

  // Preprocessor
  s = s.replace(/^(#\w+)/gm, '<span class="text-purple-400">$1</span>');

  // Keywords
  const keywords = ['int', 'uint8_t', 'uint16_t', 'uint32_t', 'uint64_t', 'size_t', 'void', 'char',
    'const', 'static', 'typedef', 'struct', 'enum', 'if', 'else', 'for', 'while', 'do',
    'return', 'break', 'continue', 'switch', 'case', 'default', 'sizeof', 'NULL',
    'include', 'define', 'ifndef', 'endif', 'extern'];
  keywords.forEach(kw => {
    const re = new RegExp(`\\b(${kw})\\b`, 'g');
    s = s.replace(re, '<span class="text-blue-400">$1</span>');
  });

  // Numbers
  s = s.replace(/\b(0x[0-9a-fA-F]+|0b[01]+|\d+[uUlL]*)\b/g, '<span class="text-amber-400">$1</span>');

  return s;
}

function FormatSection() {
  return (
    <div>
      <h2 className="text-2xl font-bold text-white mb-6">File Format Specification</h2>

      <div className="bg-gray-900/50 border border-gray-800 rounded-xl p-6 mb-8">
        <h3 className="text-lg font-bold text-purple-400 mb-4">File Header (21 bytes)</h3>
        <div className="overflow-x-auto">
          <table className="w-full text-sm border-collapse">
            <thead>
              <tr className="border-b border-gray-700">
                <th className="text-left py-2 px-3 text-gray-400">Offset</th>
                <th className="text-left py-2 px-3 text-gray-400">Size</th>
                <th className="text-left py-2 px-3 text-gray-400">Field</th>
                <th className="text-left py-2 px-3 text-gray-400">Description</th>
              </tr>
            </thead>
            <tbody>
              <tr className="border-b border-gray-800/50">
                <td className="py-2 px-3 text-gray-300 font-mono">0</td>
                <td className="py-2 px-3 text-gray-300">4 bytes</td>
                <td className="py-2 px-3 text-purple-400">magic</td>
                <td className="py-2 px-3 text-gray-400">"PONO" — identifies the format</td>
              </tr>
              <tr className="border-b border-gray-800/50">
                <td className="py-2 px-3 text-gray-300 font-mono">4</td>
                <td className="py-2 px-3 text-gray-300">1 byte</td>
                <td className="py-2 px-3 text-purple-400">version</td>
                <td className="py-2 px-3 text-gray-400">Format version (currently 1)</td>
              </tr>
              <tr className="border-b border-gray-800/50">
                <td className="py-2 px-3 text-gray-300 font-mono">5</td>
                <td className="py-2 px-3 text-gray-300">4 bytes</td>
                <td className="py-2 px-3 text-purple-400">block_size</td>
                <td className="py-2 px-3 text-gray-400">Block size in bytes (64K–4M, LE u32)</td>
              </tr>
              <tr className="border-b border-gray-800/50">
                <td className="py-2 px-3 text-gray-300 font-mono">9</td>
                <td className="py-2 px-3 text-gray-300">8 bytes</td>
                <td className="py-2 px-3 text-purple-400">original_size</td>
                <td className="py-2 px-3 text-gray-400">Total uncompressed size (LE u64)</td>
              </tr>
              <tr className="border-b border-gray-800/50">
                <td className="py-2 px-3 text-gray-300 font-mono">17</td>
                <td className="py-2 px-3 text-gray-300">4 bytes</td>
                <td className="py-2 px-3 text-purple-400">num_blocks</td>
                <td className="py-2 px-3 text-gray-400">Number of compressed blocks (LE u32)</td>
              </tr>
            </tbody>
          </table>
        </div>
      </div>

      <div className="bg-gray-900/50 border border-gray-800 rounded-xl p-6 mb-8">
        <h3 className="text-lg font-bold text-blue-400 mb-4">Block Header (12 bytes each)</h3>
        <div className="overflow-x-auto">
          <table className="w-full text-sm border-collapse">
            <thead>
              <tr className="border-b border-gray-700">
                <th className="text-left py-2 px-3 text-gray-400">Offset</th>
                <th className="text-left py-2 px-3 text-gray-400">Size</th>
                <th className="text-left py-2 px-3 text-gray-400">Field</th>
                <th className="text-left py-2 px-3 text-gray-400">Description</th>
              </tr>
            </thead>
            <tbody>
              <tr className="border-b border-gray-800/50">
                <td className="py-2 px-3 text-gray-300 font-mono">0</td>
                <td className="py-2 px-3 text-gray-300">4 bytes</td>
                <td className="py-2 px-3 text-blue-400">compressed_size</td>
                <td className="py-2 px-3 text-gray-400">Size of compressed payload (LE u32)</td>
              </tr>
              <tr className="border-b border-gray-800/50">
                <td className="py-2 px-3 text-gray-300 font-mono">4</td>
                <td className="py-2 px-3 text-gray-300">4 bytes</td>
                <td className="py-2 px-3 text-blue-400">uncompressed_size</td>
                <td className="py-2 px-3 text-gray-400">Original block size (LE u32)</td>
              </tr>
              <tr className="border-b border-gray-800/50">
                <td className="py-2 px-3 text-gray-300 font-mono">8</td>
                <td className="py-2 px-3 text-gray-300">4 bytes</td>
                <td className="py-2 px-3 text-blue-400">crc32</td>
                <td className="py-2 px-3 text-gray-400">CRC32 of uncompressed block data</td>
              </tr>
            </tbody>
          </table>
        </div>
        <p className="text-sm text-gray-500 mt-3">
          Followed by <code className="text-blue-400">compressed_size</code> bytes of range-coded payload.
        </p>
      </div>

      <div className="bg-gray-900/50 border border-gray-800 rounded-xl p-6 mb-8">
        <h3 className="text-lg font-bold text-green-400 mb-4">Visual Layout</h3>
        <div className="font-mono text-xs bg-black/40 rounded-lg p-4 overflow-x-auto">
          <pre className="text-gray-300">{`┌─────────────────────────────────────────────┐
│  FILE HEADER (21 bytes)                     │
│  ┌──────────┬─────────┬──────────────────┐  │
│  │ "PONO"   │ version │ block_size (u32) │  │
│  │ 4 bytes  │ 1 byte  │ 4 bytes          │  │
│  ├──────────┴─────────┴──────────────────┤  │
│  │ original_size (u64) │ num_blocks (u32)│  │
│  │ 8 bytes             │ 4 bytes         │  │
│  └─────────────────────┴─────────────────┘  │
├─────────────────────────────────────────────┤
│  BLOCK 0                                    │
│  ┌─────────────────┬───────────────────┐   │
│  │ comp_size (u32) │ uncomp_size (u32) │   │
│  ├─────────────────┴───────────────────┤   │
│  │ crc32 (u32)                         │   │
│  ├─────────────────────────────────────┤   │
│  │ Range-coded payload...              │   │
│  │ (comp_size bytes)                   │   │
│  └─────────────────────────────────────┘   │
├─────────────────────────────────────────────┤
│  BLOCK 1                                    │
│  ┌─────────────────────────────────────┐   │
│  │ ... (same structure)                │   │
│  └─────────────────────────────────────┘   │
├─────────────────────────────────────────────┤
│  ...                                        │
├─────────────────────────────────────────────┤
│  BLOCK N-1                                  │
│  ┌─────────────────────────────────────┐   │
│  │ ... (last block may be partial)     │   │
│  └─────────────────────────────────────┘   │
└─────────────────────────────────────────────┘`}</pre>
        </div>
      </div>

      <div className="bg-gray-900/50 border border-gray-800 rounded-xl p-6">
        <h3 className="text-lg font-bold text-amber-400 mb-4">CRC32 Implementation</h3>
        <div className="text-sm text-gray-300 space-y-3">
          <p>
            The CRC32 uses the standard polynomial <code className="text-amber-400">0xEDB88320</code> (reversed
            representation of the CRC-32/ISO-HDLC polynomial). It's computed using a 256-entry lookup table
            generated at first use.
          </p>
          <div className="bg-black/40 rounded-lg p-4 font-mono text-xs overflow-x-auto">
            <div className="text-green-400">// Table generation:</div>
            <div className="text-gray-300">for i in 0..255:</div>
            <div className="text-gray-300">{'  '}c = i</div>
            <div className="text-gray-300">{'  '}for j in 0..7:</div>
            <div className="text-gray-300">{'    '}if c & 1: c = 0xEDB88320 {'^'} (c {'>>'} 1)</div>
            <div className="text-gray-300">{'    '}else:    c = c {'>>'} 1</div>
            <div className="text-gray-300">{'  '}table[i] = c</div>
            <div className="mt-2 text-green-400">// Computation:</div>
            <div className="text-gray-300">crc = 0xFFFFFFFF</div>
            <div className="text-gray-300">for byte in data:</div>
            <div className="text-gray-300">{'  '}crc = table[(crc {'^'} byte) & 0xFF] {'^'} (crc {'>>'} 8)</div>
            <div className="text-gray-300">return crc {'^'} 0xFFFFFFFF</div>
          </div>
        </div>
      </div>
    </div>
  );
}

export default App;
