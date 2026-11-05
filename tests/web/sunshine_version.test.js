import { describe, expect, it } from 'vitest'

import SunshineVersion, {
  RELEASES_API_URL,
  preReleaseFromList,
  versionFromRelease,
} from '../../src_assets/common/assets/web/sunshine_version.js'

describe('SunshineVersion', () => {
  it('parses release metadata', () => {
    const version = new SunshineVersion({
      name: 'Sunshine release',
      tag_name: 'v2026.9.16',
      tag_tag: 'stable',
    })

    expect(version.release).toBeDefined()
    expect(version.version).toBe('v2026.9.16')
    expect(version.versionName).toBe('Sunshine release')
    expect(version.versionTag).toBe('stable')
    expect(version.versionParts).toEqual([2026, 9, 16])
    expect(version.versionMajor).toBe(2026)
    expect(version.versionMinor).toBe(9)
    expect(version.versionPatch).toBe(16)
  })

  it('parses a version string without release metadata', () => {
    const version = new SunshineVersion(null, '1.2.3')

    expect(version.release).toBeNull()
    expect(version.versionParts).toEqual([1, 2, 3])
    expect(version.versionName).toBeNull()
    expect(version.versionTag).toBeNull()
  })

  it('requires either release metadata or a version string', () => {
    expect(() => new SunshineVersion()).toThrow('Either release or version must be provided')
  })

  it('compares version objects and strings', () => {
    const version = new SunshineVersion(null, '2.0.0')

    expect(version.isGreater('1.9.9')).toBe(true)
    expect(version.isGreater(new SunshineVersion(null, '2.0.0'))).toBe(false)
    expect(version.isGreater('3.0.0')).toBe(false)
  })

  it('handles absent and invalid comparison values', () => {
    const version = new SunshineVersion(null, '2.0.0')
    const unparsedVersion = new SunshineVersion(null, 'placeholder')
    unparsedVersion.versionParts = null

    expect(version.parseVersion(null)).toBeNull()
    expect(unparsedVersion.isGreater('1.0.0')).toBe(false)
    expect(() => version.isGreater(2)).toThrow(TypeError)
  })

  it('checks this fork for releases', () => {
    expect(RELEASES_API_URL).toBe('https://api.github.com/repos/luizfbalves/Sunshine/releases')
  })

  it('builds versions only from tagged releases', () => {
    expect(versionFromRelease({ tag_name: 'v2026.1105.120000' }).versionParts).toEqual([2026, 1105, 120000])
    expect(versionFromRelease({ message: 'Not Found' })).toBeNull()
    expect(versionFromRelease(null)).toBeNull()
    expect(versionFromRelease(undefined)).toBeNull()
  })

  it('finds the newest pre-release in a release listing', () => {
    const releases = [
      { tag_name: 'v2026.1105.120000', prerelease: false },
      null,
      { tag_name: 'v2026.1104.120000', prerelease: true },
    ]

    expect(preReleaseFromList(releases).version).toBe('v2026.1104.120000')
    expect(preReleaseFromList([{ tag_name: 'v1.0.0', prerelease: false }])).toBeNull()
    expect(preReleaseFromList([])).toBeNull()
    expect(preReleaseFromList({ message: 'Not Found' })).toBeNull()
  })
})
