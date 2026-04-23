package mtpndd

import (
	"fmt"
	"math"
	"sync"

	"github.com/Augists/mtpndd-go/internal/bdd"
)

// Config exposes the tunable knobs of the mtpndd runtime. Sizes must be
// powers of two. A zero value means "use the default".
//
// Call Init(cfg) before any other API to set a non-default configuration;
// subsequent calls are ignored. NewEngine / NewEngineWithConfig wrap Init
// so most callers don't need to invoke it directly.
type Config struct {
	NDD SubConfig
	BDD SubConfig

	// SpawnPairThreshold is the minimum Cartesian-product size of two
	// edge maps at which same-field And/Or will fan out one goroutine
	// per pair. Goroutine spawn costs ~1–5 µs so this needs to stay
	// conservative. Default 4.
	SpawnPairThreshold int
}

// SubConfig holds the shared sizing knobs used by both the NDD layer and
// the internal BDD layer.
type SubConfig struct {
	// OpCacheSize is the number of slots in the op-cache. Must be a
	// power of two. Default 2^19 (~12 MB per cache).
	OpCacheSize int

	// CacheClearInterval is the number of puts between periodic full
	// cache wipes. Bounds the strong-pinned working set. Default 2^21.
	CacheClearInterval uint64

	// ShardCount is the number of unique-table shards. Must be a
	// power of two. Default 64.
	ShardCount int

	// InitialShardCap is the starting slot capacity of each shard's
	// open-addressing table. Must be a power of two. Defaults: NDD
	// 1024, BDD 512.
	InitialShardCap int

	// SlabChunkSize is the number of Nodes per slab chunk. Must be a
	// power of two. Default 2^18 (256 K nodes per chunk).
	SlabChunkSize int
}

// DefaultConfig returns the configuration tuned for the n-queens benchmark
// at N ≤ 13. Workloads with substantially larger node counts (e.g. network
// verification on big fat-trees) should raise OpCacheSize and
// InitialShardCap; see docs/benchmarking.md.
func DefaultConfig() Config {
	return Config{
		NDD: SubConfig{
			OpCacheSize:        1 << 19,
			CacheClearInterval: 1 << 21,
			ShardCount:         64,
			InitialShardCap:    1024,
			SlabChunkSize:      1 << 18,
		},
		BDD: SubConfig{
			OpCacheSize:        1 << 19,
			CacheClearInterval: 1 << 21,
			ShardCount:         64,
			InitialShardCap:    512,
			SlabChunkSize:      1 << 18,
		},
		SpawnPairThreshold: 4,
	}
}

// ActiveConfig returns the configuration that is (or will be, if Init has
// not yet run) used by the runtime.
func ActiveConfig() Config {
	return activeConfig
}

var (
	activeConfig = DefaultConfig()
	initOnce     sync.Once
)

// Init installs cfg as the runtime configuration and allocates all
// module-level structures. Must be called before any other API. A second
// call is ignored — the first caller wins.
func Init(cfg Config) {
	initOnce.Do(func() {
		validateAndApply(cfg)
	})
}

func ensureInit() {
	initOnce.Do(func() { validateAndApply(DefaultConfig()) })
}

func validateAndApply(cfg Config) {
	checkPow2("NDD.OpCacheSize", cfg.NDD.OpCacheSize)
	checkPow2("NDD.ShardCount", cfg.NDD.ShardCount)
	checkPow2("NDD.InitialShardCap", cfg.NDD.InitialShardCap)
	checkPow2("NDD.SlabChunkSize", cfg.NDD.SlabChunkSize)
	checkPow2("BDD.OpCacheSize", cfg.BDD.OpCacheSize)
	checkPow2("BDD.ShardCount", cfg.BDD.ShardCount)
	checkPow2("BDD.InitialShardCap", cfg.BDD.InitialShardCap)
	checkPow2("BDD.SlabChunkSize", cfg.BDD.SlabChunkSize)
	if cfg.SpawnPairThreshold < 1 {
		panic("mtpndd: SpawnPairThreshold must be >= 1")
	}
	activeConfig = cfg
	spawnPairThreshold = cfg.SpawnPairThreshold
	allocateNDD(cfg.NDD)
	bdd.Init(bdd.Config{
		OpCacheSize:        cfg.BDD.OpCacheSize,
		CacheClearInterval: cfg.BDD.CacheClearInterval,
		ShardCount:         cfg.BDD.ShardCount,
		InitialShardCap:    cfg.BDD.InitialShardCap,
		SlabChunkSize:      cfg.BDD.SlabChunkSize,
	})
}

func checkPow2(name string, v int) {
	if v <= 0 || v&(v-1) != 0 {
		panic(fmt.Sprintf("mtpndd: %s must be a positive power of two (got %d)", name, v))
	}
}

// Field is one logical bit-field in the NDD ordering.
type Field struct {
	ID       uint32
	BitWidth uint32

	bddVarBase uint32

	// pow2BitWidth = 2^BitWidth, pow2Base = 2^bddVarBase. Precomputed so
	// SatCount doesn't loop-multiply on every call (was 170 ms flat /
	// 1.4 % on sre-ndd fattree08 MF=3).
	pow2BitWidth float64
	pow2Base     float64

	varNodes    []*Node
	notVarNodes []*Node
}

// Engine owns field declarations.
type Engine struct {
	mu        sync.Mutex
	fields    []*Field
	finalized bool
	totalBits uint32
}

// NewEngine returns an Engine using the default configuration. If Init has
// not been called yet, DefaultConfig() is installed.
func NewEngine() *Engine {
	ensureInit()
	return &Engine{}
}

// NewEngineWithConfig installs cfg (if Init hasn't already run) and returns
// a fresh Engine.
func NewEngineWithConfig(cfg Config) *Engine {
	Init(cfg)
	return &Engine{}
}

// DeclareField appends a new field of the given bit width.
func (e *Engine) DeclareField(bitWidth uint32) *Field {
	if bitWidth == 0 {
		panic("mtpndd: field bit width must be > 0")
	}
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.finalized {
		panic("mtpndd: cannot declare field after GenerateFields")
	}
	f := &Field{
		ID:           uint32(len(e.fields)),
		BitWidth:     bitWidth,
		bddVarBase:   e.totalBits,
		pow2BitWidth: math.Ldexp(1, int(bitWidth)),
		pow2Base:     math.Ldexp(1, int(e.totalBits)),
	}
	e.fields = append(e.fields, f)
	e.totalBits += bitWidth
	return f
}

// GenerateFields finalizes the field list and materializes the positive and
// negative literal NDD nodes for each bit.
func (e *Engine) GenerateFields() {
	e.mu.Lock()
	defer e.mu.Unlock()
	if e.finalized {
		return
	}
	for _, f := range e.fields {
		f.varNodes = make([]*Node, f.BitWidth)
		f.notVarNodes = make([]*Node, f.BitWidth)
		for b := uint32(0); b < f.BitWidth; b++ {
			bddVar := f.bddVarBase + b
			f.varNodes[b] = mk(f.ID, []edge{{child: True, label: bdd.IthVar(bddVar)}})
			f.notVarNodes[b] = mk(f.ID, []edge{{child: True, label: bdd.NIthVar(bddVar)}})
		}
	}
	e.finalized = true
}

// Finalized reports whether GenerateFields has been called.
func (e *Engine) Finalized() bool {
	e.mu.Lock()
	defer e.mu.Unlock()
	return e.finalized
}

// Field returns the field with the given id.
func (e *Engine) Field(id uint32) *Field {
	if int(id) >= len(e.fields) {
		panic(fmt.Sprintf("mtpndd: field id %d out of range", id))
	}
	return e.fields[id]
}

// NumFields returns the number of declared fields.
func (e *Engine) NumFields() int { return len(e.fields) }

// TotalBits returns the total number of BDD variables across all fields.
func (e *Engine) TotalBits() uint32 { return e.totalBits }

// Var returns the NDD representing "bit `bit` of field `f` is 1".
func (e *Engine) Var(f *Field, bit uint32) *Node {
	if !e.finalized {
		panic("mtpndd: call GenerateFields before Var")
	}
	return f.varNodes[bit]
}

// NotVar returns the NDD representing "bit `bit` of field `f` is 0".
func (e *Engine) NotVar(f *Field, bit uint32) *Node {
	if !e.finalized {
		panic("mtpndd: call GenerateFields before NotVar")
	}
	return f.notVarNodes[bit]
}

// BDDVar returns the raw BDD variable index for bit `bit` of field `f`.
func (e *Engine) BDDVar(f *Field, bit uint32) uint32 {
	return f.bddVarBase + bit
}
