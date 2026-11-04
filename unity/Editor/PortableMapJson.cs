using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Text;

namespace Radbot.MapAuthoring.Editor
{
    // Bounded JSON reader for presence checks. JsonUtility cannot distinguish omitted nested objects.
    internal sealed class PortableMapJson
    {
        readonly string source; int offset;
        PortableMapJson(string source) {this.source=source;}
        internal static Dictionary<string,object> Read(string source)
        {
            var reader=new PortableMapJson(source); var value=reader.Value(0) as Dictionary<string,object>; reader.Space();
            if(value==null || reader.offset!=source.Length) throw new InvalidDataException("Invalid map JSON."); return value;
        }
        void Space() {while(offset<source.Length && char.IsWhiteSpace(source[offset])) offset++;}
        char Next() {if(offset>=source.Length) throw new InvalidDataException("Truncated JSON."); return source[offset++];}
        object Value(int depth)
        {
            if(depth>64) throw new InvalidDataException("Map JSON nesting limit exceeded."); Space(); char c=Next();
            if(c=='"') return String();
            if(c=='{')
            {
                var result=new Dictionary<string,object>(); Space(); if(offset<source.Length && source[offset]=='}') {offset++; return result;}
                while(true) {Space(); if(Next()!='"') throw new InvalidDataException("Invalid JSON property."); string key=String(); Space(); if(Next()!=':' || result.ContainsKey(key)) throw new InvalidDataException("Invalid or duplicate JSON property."); result.Add(key,Value(depth+1)); Space(); c=Next(); if(c=='}') return result; if(c!=',') throw new InvalidDataException("Invalid JSON object.");}
            }
            if(c=='[')
            {
                var result=new List<object>(); Space(); if(offset<source.Length && source[offset]==']') {offset++; return result;}
                while(true) {result.Add(Value(depth+1)); Space(); c=Next(); if(c==']') return result; if(c!=',') throw new InvalidDataException("Invalid JSON array.");}
            }
            int start=offset-1; while(offset<source.Length && ",]} \t\n\r".IndexOf(source[offset])<0) offset++;
            string token=source.Substring(start,offset-start);
            if(token=="null") return null; if(token=="true") return true; if(token=="false") return false;
            if(double.TryParse(token,NumberStyles.Float,CultureInfo.InvariantCulture,out double number) && !double.IsNaN(number) && !double.IsInfinity(number)) return number;
            throw new InvalidDataException("Invalid JSON value.");
        }
        string String()
        {
            var result=new StringBuilder();
            while(true)
            {
                char c=Next(); if(c=='"') return result.ToString(); if(c<32) throw new InvalidDataException("Invalid JSON string.");
                if(c!='\\') {result.Append(c); continue;}
                c=Next(); switch(c)
                {
                    case '"': case '\\': case '/': result.Append(c); break;
                    case 'b': result.Append('\b'); break; case 'f': result.Append('\f'); break;
                    case 'n': result.Append('\n'); break; case 'r': result.Append('\r'); break; case 't': result.Append('\t'); break;
                    case 'u': string hex=""; for(int i=0;i<4;i++) hex+=Next(); if(!ushort.TryParse(hex,NumberStyles.HexNumber,CultureInfo.InvariantCulture,out ushort value)) throw new InvalidDataException("Invalid Unicode escape."); result.Append((char)value); break;
                    default: throw new InvalidDataException("Invalid JSON escape.");
                }
            }
        }
    }
}
